extern crate mesa_rust;
extern crate mesa_rust_gen;
extern crate rusticl_opencl_gen;

use crate::api::icd::*;
use crate::core::device::*;
use crate::core::event::*;
use crate::core::memory::*;
use crate::core::program::*;
use crate::core::queue::*;
use crate::impl_cl_type_trait;

use self::mesa_rust::compiler::clc::*;
use self::mesa_rust::compiler::nir::*;
use self::mesa_rust_gen::*;
use self::rusticl_opencl_gen::*;

use std::cell::RefCell;
use std::collections::HashMap;
use std::collections::HashSet;
use std::convert::TryInto;
use std::ptr;
use std::sync::Arc;

// ugh, we are not allowed to take refs, so...
pub enum KernelArgValue {
    None,
    Constant(Vec<u8>),
    MemObject(&'static Mem),
    Sampler(&'static Sampler),
    LocalMem(usize),
}

#[derive(PartialEq, Eq)]
pub enum KernelArgType {
    Constant, // for anything passed by value
    Sampler,
    MemGlobal,
    MemConstant,
    MemLocal,
}

#[derive(Hash, PartialEq, Eq)]
pub enum InternalKernelArgType {
    ConstantBuffer,
}

pub struct KernelArg {
    spirv: spirv::SPIRVKernelArg,
    pub kind: KernelArgType,
    pub size: usize,
    pub offset: usize,
    pub dead: bool,
}

#[derive(Hash, PartialEq, Eq)]
pub struct InternalKernelArg {
    pub kind: InternalKernelArgType,
    pub size: usize,
    pub offset: usize,
}

impl KernelArg {
    fn from_spirv_nir(spirv: Vec<spirv::SPIRVKernelArg>, nir: &mut NirShader) -> Vec<Self> {
        let nir_arg_map: HashMap<_, _> = nir
            .variables_with_mode(
                nir_variable_mode::nir_var_uniform | nir_variable_mode::nir_var_image,
            )
            .map(|v| (v.data.location, v))
            .collect();
        let mut res = Vec::new();

        for (i, s) in spirv.into_iter().enumerate() {
            let nir = nir_arg_map.get(&(i as i32)).unwrap();
            let kind = match s.address_qualifier {
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_PRIVATE => {
                    if unsafe { glsl_type_is_sampler(nir.type_) } {
                        KernelArgType::Sampler
                    } else {
                        KernelArgType::Constant
                    }
                }
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_CONSTANT => {
                    KernelArgType::MemConstant
                }
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_LOCAL => {
                    KernelArgType::MemLocal
                }
                clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_GLOBAL => {
                    KernelArgType::MemGlobal
                }
            };

            res.push(Self {
                spirv: s,
                size: unsafe { glsl_get_cl_size(nir.type_) } as usize,
                // we'll update it later in the 2nd pass
                kind: kind,
                offset: 0,
                dead: true,
            });
        }
        res
    }

    fn assign_locations(
        args: &mut [Self],
        internal_args: &mut [InternalKernelArg],
        nir: &mut NirShader,
    ) {
        for var in nir.variables_with_mode(
            nir_variable_mode::nir_var_uniform | nir_variable_mode::nir_var_image,
        ) {
            if let Some(arg) = args.get_mut(var.data.location as usize) {
                arg.offset = var.data.driver_location as usize;
                arg.dead = false;
            } else {
                internal_args
                    .get_mut(var.data.location as usize - args.len())
                    .unwrap()
                    .offset = var.data.driver_location as usize;
            }
        }
    }
}

#[repr(C)]
pub struct Kernel {
    pub base: CLObjectBase<CL_INVALID_KERNEL>,
    pub prog: Arc<Program>,
    pub name: String,
    pub args: Vec<KernelArg>,
    pub values: Vec<RefCell<Option<KernelArgValue>>>,
    internal_args: Vec<InternalKernelArg>,
    nirs: HashMap<Arc<Device>, NirShader>,
}

impl_cl_type_trait!(cl_kernel, Kernel, CL_INVALID_KERNEL);

fn create_kernel_arr(vals: &[usize], val: u32) -> [u32; 3] {
    let mut res = [val; 3];
    for (i, v) in vals.iter().enumerate() {
        res[i] = (*v).try_into().expect("64 bit work groups not supported");
    }
    res
}

// mostly like clc_spirv_to_dxil
// does not DCEe uniforms or images!
fn lower_and_optimize_nir_pre_inputs(nir: &mut NirShader, lib_clc: &NirShader) {
    nir.set_workgroup_size(&[0; 3]);
    nir.structurize();
    while {
        let mut progress = false;
        progress |= nir.pass0(nir_copy_prop);
        progress |= nir.pass0(nir_opt_copy_prop_vars);
        progress |= nir.pass0(nir_opt_deref);
        progress |= nir.pass0(nir_opt_dce);
        progress |= nir.pass0(nir_opt_undef);
        progress |= nir.pass0(nir_opt_constant_folding);
        progress |= nir.pass0(nir_opt_cse);
        progress |= nir.pass0(nir_lower_vars_to_ssa);
        progress |= nir.pass0(nir_opt_algebraic);
        progress
    } {}
    nir.inline(lib_clc);
    nir.remove_non_entrypoints();
    while {
        let mut progress = false;
        progress |= nir.pass0(nir_copy_prop);
        progress |= nir.pass0(nir_opt_copy_prop_vars);
        progress |= nir.pass0(nir_opt_deref);
        progress |= nir.pass0(nir_opt_dce);
        progress |= nir.pass0(nir_opt_undef);
        progress |= nir.pass0(nir_opt_constant_folding);
        progress |= nir.pass0(nir_opt_cse);
        progress |= nir.pass0(nir_split_var_copies);
        progress |= nir.pass0(nir_lower_var_copies);
        progress |= nir.pass0(nir_lower_vars_to_ssa);
        progress |= nir.pass0(nir_opt_algebraic);
        progress |= nir.pass1(
            nir_opt_if,
            nir_opt_if_options::nir_opt_if_aggressive_last_continue
                | nir_opt_if_options::nir_opt_if_optimize_phi_true_false,
        );
        progress |= nir.pass0(nir_opt_dead_cf);
        progress |= nir.pass0(nir_opt_remove_phis);
        // we don't want to be too aggressive here, but it kills a bit of CFG
        progress |= nir.pass3(nir_opt_peephole_select, 1, true, true);
        progress |= nir.pass1(
            nir_lower_vec3_to_vec4,
            nir_variable_mode::nir_var_mem_generic | nir_variable_mode::nir_var_uniform,
        );
        progress
    } {}
    // TODO variable initializers
    // TODO lower memcpy
    nir.pass0(nir_dedup_inline_samplers);
    nir.pass2(
        nir_lower_vars_to_explicit_types,
        nir_variable_mode::nir_var_function_temp,
        Some(glsl_get_cl_type_size_align),
    );
    // TODO printf
    nir.pass0(nir_split_var_copies);
    nir.pass0(nir_opt_copy_prop_vars);
    nir.pass0(nir_lower_var_copies);
    nir.pass0(nir_lower_vars_to_ssa);
    nir.pass0(nir_lower_alu);
    nir.pass0(nir_opt_dce);
    nir.pass0(nir_opt_deref);
}

fn lower_and_optimize_nir_late(
    dev: &Device,
    nir: &mut NirShader,
    args: usize,
) -> Vec<InternalKernelArg> {
    let mut res = Vec::new();
    let mut lower_state = rusticl_lower_state::default();

    nir.pass2(
        nir_remove_dead_variables,
        nir_variable_mode::nir_var_uniform
            | nir_variable_mode::nir_var_mem_constant
            | nir_variable_mode::nir_var_function_temp,
        ptr::null(),
    );
    nir.pass1(nir_lower_readonly_images_to_tex, false);
    nir.pass2(
        nir_remove_dead_variables,
        nir_variable_mode::nir_var_mem_shared | nir_variable_mode::nir_var_function_temp,
        ptr::null(),
    );
    nir.reset_scratch_size();
    nir.pass2(
        nir_lower_vars_to_explicit_types,
        nir_variable_mode::nir_var_mem_constant,
        Some(glsl_get_cl_type_size_align),
    );
    nir.extract_constant_initializers();
    // TODO printf
    // TODO 32 bit devices
    if nir.has_constant() {
        res.push(InternalKernelArg {
            kind: InternalKernelArgType::ConstantBuffer,
            offset: 0,
            size: 8,
        });
        lower_state.const_buf = nir.add_var(
            nir_variable_mode::nir_var_uniform,
            unsafe { glsl_uint64_t_type() },
            args + res.len() - 1,
            "constant_buffer_addr",
        );
    }

    nir.pass2(
        nir_lower_vars_to_explicit_types,
        nir_variable_mode::nir_var_mem_shared
            | nir_variable_mode::nir_var_function_temp
            | nir_variable_mode::nir_var_uniform
            | nir_variable_mode::nir_var_mem_global,
        Some(glsl_get_cl_type_size_align),
    );
    nir.pass2(
        nir_lower_explicit_io,
        nir_variable_mode::nir_var_mem_global | nir_variable_mode::nir_var_mem_constant,
        nir_address_format::nir_address_format_64bit_global,
    );
    nir.pass1(rusticl_lower_intrinsics, &mut lower_state);
    nir.pass2(
        nir_lower_explicit_io,
        nir_variable_mode::nir_var_mem_shared
            | nir_variable_mode::nir_var_function_temp
            | nir_variable_mode::nir_var_uniform,
        nir_address_format::nir_address_format_32bit_offset_as_64bit,
    );
    nir.pass0(nir_lower_system_values);
    let compute_options = nir_lower_compute_system_values_options::default();
    nir.pass1(nir_lower_compute_system_values, &compute_options);
    nir.pass0(nir_opt_deref);
    nir.pass0(nir_lower_vars_to_ssa);

    // TODO whatever clc is doing here

    nir.pass1(nir_lower_convert_alu_types, None);
    nir.pass0(nir_opt_dce);
    dev.screen.finalize_nir(nir);
    res
}

impl Kernel {
    pub fn new(
        name: String,
        prog: Arc<Program>,
        mut nirs: HashMap<Arc<Device>, NirShader>,
        args: Vec<spirv::SPIRVKernelArg>,
    ) -> Arc<Kernel> {
        nirs.iter_mut()
            .for_each(|(d, n)| lower_and_optimize_nir_pre_inputs(n, &d.lib_clc));
        let nir = nirs.values_mut().next().unwrap();
        let mut args = KernelArg::from_spirv_nir(args, nir);
        // can't use vec!...
        let values = args.iter().map(|_| RefCell::new(None)).collect();
        let internal_args: HashSet<_> = nirs
            .iter_mut()
            .map(|(d, n)| lower_and_optimize_nir_late(d, n, args.len()))
            .collect();
        // we want the same internal args for every compiled kernel, for now
        assert!(internal_args.len() == 1);
        let mut internal_args = internal_args.into_iter().next().unwrap();

        nirs.values_mut()
            .for_each(|n| KernelArg::assign_locations(&mut args, &mut internal_args, n));

        Arc::new(Self {
            base: CLObjectBase::new(),
            prog: prog,
            name: name,
            args: args,
            values: values,
            internal_args: internal_args,
            // caller has to verify all kernels have the same sig
            nirs: nirs,
        })
    }

    // the painful part is, that host threads are allowed to modify the kernel object once it was
    // enqueued, so return a closure with all req data included.
    pub fn launch(
        &self,
        q: &Arc<Queue>,
        work_dim: u32,
        block: &[usize],
        grid: &[usize],
        offsets: &[usize],
    ) -> EventSig {
        let nir = self.nirs.get(&q.device).unwrap();
        let mut block = create_kernel_arr(block, 1);
        let mut grid = create_kernel_arr(grid, 1);
        let offsets = create_kernel_arr(offsets, 0);
        let mut input: Vec<u8> = Vec::new();
        let mut resource_info = Vec::new();
        let mut local_size: u32 = nir.shared_size();

        for i in 0..3 {
            if block[i] == 0 {
                block[i] = 1;
            } else {
                grid[i] /= block[i];
            }
        }

        for (arg, val) in self.args.iter().zip(&self.values) {
            if arg.dead {
                continue;
            }
            match val.borrow().as_ref().unwrap() {
                KernelArgValue::Constant(c) => input.extend_from_slice(c),
                KernelArgValue::MemObject(mem) => {
                    input.extend_from_slice(&mem.offset.to_ne_bytes());
                    resource_info.push((Some(mem.get_res_of_dev(&q.device).clone()), arg.offset));
                }
                KernelArgValue::LocalMem(size) => {
                    // TODO 32 bit
                    input.extend_from_slice(&[0; 8]);
                    local_size += *size as u32;
                }
                KernelArgValue::None => {
                    assert!(
                        arg.kind == KernelArgType::MemGlobal
                            || arg.kind == KernelArgType::MemConstant
                    );
                    input.extend_from_slice(&[0; 8]);
                }
                _ => panic!("unhandled arg type"),
            }
        }

        for arg in &self.internal_args {
            match arg.kind {
                InternalKernelArgType::ConstantBuffer => {
                    input.extend_from_slice(&[0; 8]);
                    let buf = nir.get_constant_buffer();
                    let res = Arc::new(
                        q.device
                            .screen()
                            .resource_create_buffer(buf.len() as u32)
                            .unwrap(),
                    );
                    q.device.helper_ctx().buffer_subdata(
                        &res,
                        0,
                        buf.as_ptr().cast(),
                        buf.len() as u32,
                    );
                    resource_info.push((Some(res), arg.offset));
                }
            }
        }

        let cso = q
            .device
            .helper_ctx()
            .create_compute_state(nir, input.len() as u32, local_size);

        Box::new(move |_, ctx| {
            let mut input = input.clone();
            let mut resources = Vec::with_capacity(resource_info.len());
            let mut globals: Vec<*mut u32> = Vec::new();

            for (res, offset) in resource_info.clone() {
                resources.push(res);
                globals.push(unsafe { input.as_mut_ptr().add(offset) }.cast());
            }

            ctx.bind_compute_state(cso);
            ctx.set_global_binding(resources.as_slice(), &mut globals);
            ctx.launch_grid(work_dim, block, grid, offsets, &input);
            ctx.clear_global_binding(globals.len() as u32);
            ctx.delete_compute_state(cso);
            ctx.memory_barrier(PIPE_BARRIER_GLOBAL_BUFFER);
            Ok(())
        })
    }

    pub fn access_qualifier(&self, idx: cl_uint) -> cl_kernel_arg_access_qualifier {
        let aq = self.args[idx as usize].spirv.access_qualifier;

        if aq
            == clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_READ
                | clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_WRITE
        {
            CL_KERNEL_ARG_ACCESS_READ_WRITE
        } else if aq == clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_READ {
            CL_KERNEL_ARG_ACCESS_READ_ONLY
        } else if aq == clc_kernel_arg_access_qualifier::CLC_KERNEL_ARG_ACCESS_WRITE {
            CL_KERNEL_ARG_ACCESS_WRITE_ONLY
        } else {
            CL_KERNEL_ARG_ACCESS_NONE
        }
    }

    pub fn address_qualifier(&self, idx: cl_uint) -> cl_kernel_arg_address_qualifier {
        match self.args[idx as usize].spirv.address_qualifier {
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_PRIVATE => {
                CL_KERNEL_ARG_ADDRESS_PRIVATE
            }
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_CONSTANT => {
                CL_KERNEL_ARG_ADDRESS_CONSTANT
            }
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_LOCAL => {
                CL_KERNEL_ARG_ADDRESS_LOCAL
            }
            clc_kernel_arg_address_qualifier::CLC_KERNEL_ARG_ADDRESS_GLOBAL => {
                CL_KERNEL_ARG_ADDRESS_GLOBAL
            }
        }
    }

    pub fn type_qualifier(&self, idx: cl_uint) -> cl_kernel_arg_type_qualifier {
        let tq = self.args[idx as usize].spirv.type_qualifier;
        let zero = clc_kernel_arg_type_qualifier(0);
        let mut res = CL_KERNEL_ARG_TYPE_NONE;

        if tq & clc_kernel_arg_type_qualifier::CLC_KERNEL_ARG_TYPE_CONST != zero {
            res |= CL_KERNEL_ARG_TYPE_CONST;
        }

        if tq & clc_kernel_arg_type_qualifier::CLC_KERNEL_ARG_TYPE_RESTRICT != zero {
            res |= CL_KERNEL_ARG_TYPE_RESTRICT;
        }

        if tq & clc_kernel_arg_type_qualifier::CLC_KERNEL_ARG_TYPE_VOLATILE != zero {
            res |= CL_KERNEL_ARG_TYPE_VOLATILE;
        }

        res.into()
    }

    pub fn arg_name(&self, idx: cl_uint) -> &String {
        &self.args[idx as usize].spirv.name
    }

    pub fn arg_type_name(&self, idx: cl_uint) -> &String {
        &self.args[idx as usize].spirv.type_name
    }

    pub fn priv_mem_size(&self, dev: &Arc<Device>) -> cl_ulong {
        self.nirs.get(dev).unwrap().scratch_size() as cl_ulong
    }

    pub fn local_mem_size(&self, dev: &Arc<Device>) -> cl_ulong {
        // TODO include args
        self.nirs.get(dev).unwrap().shared_size() as cl_ulong
    }
}