/*******************************************************************************
* Copyright 2019-2020 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef SYCL_ENGINE_BASE_HPP
#define SYCL_ENGINE_BASE_HPP

#include <memory>

#include "common/c_types_map.hpp"
#include "common/engine.hpp"
#include "common/memory_storage.hpp"
#include "common/stream.hpp"
#include "gpu/compute/compute.hpp"
#include "gpu/ocl/ocl_engine.hpp"
#include "gpu/ocl/ocl_gpu_engine.hpp"
#include "gpu/ocl/ocl_utils.hpp"
#include "sycl/sycl_device_info.hpp"
#include "sycl/sycl_ocl_gpu_kernel.hpp"
#include "sycl/sycl_utils.hpp"

#include <CL/sycl.hpp>

namespace dnnl {
namespace impl {
namespace sycl {

class sycl_engine_base_t : public gpu::compute::compute_engine_t {
public:
    sycl_engine_base_t(engine_kind_t kind, const cl::sycl::device &dev,
            const cl::sycl::context &ctx)
        : gpu::compute::compute_engine_t(
                kind, runtime_kind::sycl, new sycl_device_info_t(dev))
        , device_(dev)
        , context_(ctx) {}

    status_t init() {
        CHECK(gpu::compute::compute_engine_t::init());

        backend_ = get_sycl_backend(device_);
        if (!utils::one_of(backend_, backend_t::host, backend_t::opencl,
                    backend_t::level0))
            return status::invalid_arguments;

        stream_t *service_stream_ptr;
        status_t status = create_stream(
                &service_stream_ptr, stream_flags::default_flags, nullptr);
        if (status != status::success) return status;
        service_stream_.reset(service_stream_ptr);
        return status::success;
    }

    virtual status_t create_memory_storage(memory_storage_t **storage,
            unsigned flags, size_t size, void *handle) override;

    virtual status_t create_stream(stream_t **stream, unsigned flags,
            const stream_attr_t *attr) override;
    status_t create_stream(stream_t **stream, cl::sycl::queue &queue);

    virtual status_t create_kernel(gpu::compute::kernel_t *kernel,
            const char *kernel_name,
            const std::vector<unsigned char> &binary) const override {
        if (kind() != engine_kind::gpu) {
            assert("not expected");
            return status::invalid_arguments;
        }

        *kernel = gpu::compute::kernel_t(
                new gpu::ocl::ocl_gpu_kernel_t(binary, kernel_name));
        return status::success;
    }

    virtual status_t create_kernels(
            std::vector<gpu::compute::kernel_t> *kernels,
            const std::vector<const char *> &kernel_names,
            const gpu::compute::kernel_ctx_t &kernel_ctx) const override {
        if (kind() != engine_kind::gpu) {
            assert("not expected");
            return status::invalid_arguments;
        }
        gpu::ocl::ocl_engine_factory_t f(engine_kind::gpu);
        std::unique_ptr<gpu::ocl::ocl_gpu_engine_t> ocl_engine;

        if (backend_ == backend_t::opencl) {
            engine_t *ocl_engine_ptr;
            CHECK(f.engine_create(
                    &ocl_engine_ptr, ocl_device(), ocl_context()));
            ocl_engine.reset(utils::downcast<gpu::ocl::ocl_gpu_engine_t *>(
                    ocl_engine_ptr));
        } else if (backend_ == backend_t::level0) {
            engine_t *ocl_engine_ptr;
            // FIXME: This does not work for multi-GPU systems. OpenCL engine
            // should be created based on the Level0 device to ensure that a
            // program is compiled for the same physical device. However,
            // OpenCL does not provide any API to match its devices with
            // Level0.
            CHECK(f.engine_create(&ocl_engine_ptr, 0));
            ocl_engine.reset(utils::downcast<gpu::ocl::ocl_gpu_engine_t *>(
                    ocl_engine_ptr));
        } else {
            assert(!"not expected");
            return status::invalid_arguments;
        }

        std::vector<gpu::compute::kernel_t> ocl_kernels;
        CHECK(ocl_engine->create_kernels(
                &ocl_kernels, kernel_names, kernel_ctx));
        *kernels = std::vector<gpu::compute::kernel_t>(kernel_names.size());
        for (size_t i = 0; i < ocl_kernels.size(); ++i) {
            if (!ocl_kernels[i]) continue;
            auto *k = utils::downcast<gpu::ocl::ocl_gpu_kernel_t *>(
                    ocl_kernels[i].impl());
            (*kernels)[i] = gpu::compute::kernel_t(
                    new sycl_ocl_gpu_kernel_t(k->binary(), k->name()));
        }
        return status::success;
    }

    const cl::sycl::device &device() const { return device_; }
    const cl::sycl::context &context() const { return context_; }

    backend_t backend() const { return backend_; }

    stream_t *service_stream() const override { return service_stream_.get(); }

    cl_device_id ocl_device() const {
        assert(device_.is_cpu() || device_.is_gpu());
        return gpu::ocl::make_ocl_wrapper(device().get());
    }
    cl_context ocl_context() const {
        assert(device_.is_cpu() || device_.is_gpu());
        return gpu::ocl::make_ocl_wrapper(context().get());
    }

    virtual device_id_t device_id() const override {
        return sycl_device_id(device_);
    }

private:
    cl::sycl::device device_;
    cl::sycl::context context_;

    backend_t backend_;

    std::unique_ptr<stream_t> service_stream_;
};

} // namespace sycl
} // namespace impl
} // namespace dnnl

#endif
