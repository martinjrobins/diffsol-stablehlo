#include <iree/compiler/embedding_api.h>
#include <iree/compiler/loader.h>

#define IREE_COMPILER_EXPECTED_API_MAJOR 1 // At most this major version
#define IREE_COMPILER_EXPECTED_API_MINOR 2 // At least this minor version

typedef struct compiler_t {
  iree_compiler_session_t *session;
  iree_compiler_source_t *source;
  iree_compiler_output_t *output;
  iree_compiler_invocation_t *inv;
} compiler_t;

void compiler_destroy(compiler_t* s) {
  if (s->inv)
    ireeCompilerInvocationDestroy(s->inv);
  if (s->output)
    ireeCompilerOutputDestroy(s->output);
  if (s->source)
    ireeCompilerSourceDestroy(s->source);
  if (s->session)
    ireeCompilerSessionDestroy(s->session);
  ireeCompilerGlobalShutdown();
  free(s);
}

compiler_t* compiler_create() {
  // Load the compiler library then initialize it.
  // This should be done only once per process. If deferring the load or using
  // multiple threads, be sure to synchronize this, e.g. with std::call_once.
  bool result = ireeCompilerLoadLibrary(argv[1]);
  if (!result) {
    fprintf(stderr, "** Failed to initialize IREE Compiler **\n");
    return 1;
  }
  // Note: this must be balanced with a call to ireeCompilerGlobalShutdown().
  ireeCompilerGlobalInitialize();

  // To set global options (see `iree-compile --help` for possibilities), use
  // |ireeCompilerGetProcessCLArgs| and |ireeCompilerSetupGlobalCL| here.
  // For an example of how to splice flags between a wrapping application and
  // the IREE compiler, see the "ArgParser" class in iree-run-mlir-main.cc.

  // Check the API version before proceeding any further.
  uint32_t api_version = (uint32_t)ireeCompilerGetAPIVersion();
  uint16_t api_version_major = (uint16_t)((api_version >> 16) & 0xFFFFUL);
  uint16_t api_version_minor = (uint16_t)(api_version & 0xFFFFUL);
  fprintf(stdout, "Compiler API version: %" PRIu16 ".%" PRIu16 "\n",
          api_version_major, api_version_minor);
  if (api_version_major > IREE_COMPILER_EXPECTED_API_MAJOR ||
      api_version_minor < IREE_COMPILER_EXPECTED_API_MINOR) {
    fprintf(stderr,
            "Error: incompatible API version; built for version %" PRIu16
            ".%" PRIu16 " but loaded version %" PRIu16 ".%" PRIu16 "\n",
            IREE_COMPILER_EXPECTED_API_MAJOR, IREE_COMPILER_EXPECTED_API_MINOR,
            api_version_major, api_version_minor);
    ireeCompilerGlobalShutdown();
    return 1;
  }

  // Check for a build tag with release version information.
  const char *revision = ireeCompilerGetRevision();
  fprintf(stdout, "Compiler revision: '%s'\n", revision);

  // ------------------------------------------------------------------------ //
  // Initialization and version checking complete, ready to use the compiler. //
  // ------------------------------------------------------------------------ //

  compiler_t* s = malloc(sizeof(compiler_t));
  s->session = NULL;
  s->source = NULL;
  s->output = NULL;
  s->inv = NULL;

  // A session represents a scope where one or more invocations can be executed.
  s->session = ireeCompilerSessionCreate();

  return s;
}

iree_compiler_output_t *compiler_compile(compiler_t* s, const char* source, size_t source_len) {
  // Create a source from the given string.
  s->source = ireeCompilerSourceCreateFromContent(source, source_len, "module.mlir");

  // Create an invocation to compile the source to the desired output format.
  s->inv = ireeCompilerInvocationCreate(s->source, s->output);

  // Execute the invocation.
  iree_compiler_error_t *error = NULL;
  ireeCompilerInvocationExecute(s->inv, &error);
  if (error) {
    handle_compiler_error(error);
    return 1;
  }

  // Run the compiler invocation pipeline.
  if (!ireeCompilerInvocationPipeline(inv, IREE_COMPILER_PIPELINE_STD)) {
    fprintf(stderr, "Error running compiler invocation\n");
    return ;
  }
  fprintf(stdout, "Compilation successful, output:\n\n");

  // Create a compiler 'output' piped to the 'stdout' file descriptor.
  // A file or memory buffer could be opened instead using
  // |ireeCompilerOutputOpenFile| or |ireeCompilerOutputOpenMembuffer|.
  iree_compiler_output_t *output;
  error = ireeCompilerOutputOpenMembuffer(&output);
  if (error) {
    fprintf(stderr, "Error output mem buffer\n");
    handle_compiler_error(error);
    return 1;
  }

  return output;

}

void handle_compiler_error(iree_compiler_error_t *error) {
  const char *msg = ireeCompilerErrorGetMessage(error);
  fprintf(stderr, "Error from compiler API:\n%s\n", msg);
  ireeCompilerErrorDestroy(error);
}

typedef struct runtime_t {
  iree_runtime_instance_t *instance;
  iree_runtime_session_t *session;
} runtime_t;

runtime_t* runtime_create() {
  runtime_t* r = malloc(sizeof(runtime_t));
  r->instance = NULL;
  r->session = NULL;

  // Set up the shared runtime instance.
  // An application should usually only have one of these and share it across
  // all of the sessions it has. The instance is thread-safe, while the
  // sessions are only thread-compatible (you need to lock if its required).
  iree_runtime_instance_options_t instance_options;
  iree_runtime_instance_options_initialize(&instance_options);
  iree_runtime_instance_options_use_all_available_drivers(&instance_options);
  iree_status_t status = iree_runtime_instance_create(
      &instance_options, iree_allocator_system(), &r->instance);

  // TODO(#5724): move device selection into the compiled modules.
  iree_hal_device_t* device = NULL;
  IREE_RETURN_IF_ERROR(iree_runtime_instance_try_create_default_device(
      instance, iree_make_cstring_view("local-task"), &device));

  // Set up the session to run the module.
  // Sessions are like OS processes and are used to isolate modules from each
  // other and hold runtime state such as the variables used within the module.
  // The same module loaded into two sessions will see their own private state.
  iree_runtime_session_options_t session_options;
  iree_runtime_session_options_initialize(&session_options);
  iree_status_t status = iree_runtime_session_create_with_device(
      instance, &session_options, device,
      iree_runtime_instance_host_allocator(instance), &r->session);
  iree_hal_device_release(device);

  return r;
}

void runtime_load_module(runtime_t* r, iree_compiler_output_t* output) {
  // Load the compiled module into the session.
  // This will parse the binary format and prepare it for execution.
  iree_status_t status = iree_runtime_session_load_from_memory(
      r->session, iree_make_const_byte_span(output->data, output->size));
  if (!iree_status_is_ok(status)) {
    fprintf(stderr, "Error loading module: %s\n",
            iree_status_code_string(status));
    iree_status_ignore(status);
    return 1;
  }
}


void runtime_destroy(runtime_t* r) {
  // Release the shared instance - it will be deallocated when all sessions
  // using it have been released (here it is deallocated immediately).
  iree_runtime_instance_release(r->instance);
  free(r);
}

typedef struct struct iree_jit_t {
  compiler_t *compiler;
  runtime_t *runtime;

  iree_runtime_call_t    *residual;
  iree_hal_buffer_view_t *residual_arg0_time;
  iree_hal_buffer_view_t *residual_arg1_u;
  iree_hal_buffer_view_t *residual_arg1_up;
  iree_hal_buffer_view_t *residual_out0_res;

  iree_compiler_output_t *residual_grad;
  iree_hal_buffer_view_t *residual_grad_arg0_time;
  iree_hal_buffer_view_t *residual_grad_arg1_u;
  iree_hal_buffer_view_t *residual_grad_arg2_du;
  iree_hal_buffer_view_t *residual_grad_arg3_up;
  iree_hal_buffer_view_t *residual_grad_arg4_dup;
  iree_hal_buffer_view_t *residual_grad_out0_res;
  iree_hal_buffer_view_t *residual_grad_out1_dres;

 
} iree_jit_t;

iree_jit_t* iree_jit_create(const char* source, size_t source_len) {
  iree_jit_t jit = malloc(sizeof(iree_jit_t));
  jit.compiler = compiler_create();
  jit.runtime = runtime_create();
  iree_compiler_output_t* output = compiler_compile(jit.compiler, source, source_len);
  runtime_load_module(jit.runtime, output);
  iree_jit_setup_residual_call(jit);
  iree_jit_setup_residual_grad_call(jit);
}

void iree_jit_setup_residual_call(iree_jit_t* jit) {
  // Initialize the call to the function.
  iree_runtime_call_t call;
  iree_runtime_session_t *session = jit->runtime->session;
  IREE_RETURN_IF_ERROR(iree_runtime_call_initialize_by_name(
      session, iree_make_cstring_view("module.residual"), &call));

  // Append the function inputs with the HAL device allocator in use by the
  // session. The buffers will be usable within the session and _may_ be usable
  // in other sessions depending on whether they share a compatible device.
  iree_hal_device_t* device = iree_runtime_session_device(session);
  iree_hal_allocator_t* device_allocator =
      iree_runtime_session_device_allocator(session);
  iree_allocator_t host_allocator =
      iree_runtime_session_host_allocator(session);
  iree_status_t status = iree_ok_status();
  {
    // %arg0: tensor<4xf32>
    iree_hal_buffer_view_t* arg0 = NULL;
    if (iree_status_is_ok(status)) {
      static const iree_hal_dim_t arg0_shape[1] = {4};
      static const float arg0_data[4] = {1.0f, 1.1f, 1.2f, 1.3f};
      status = iree_hal_buffer_view_allocate_buffer_copy(
          device, device_allocator,
          // Shape rank and dimensions:
          IREE_ARRAYSIZE(arg0_shape), arg0_shape,
          // Element type:
          IREE_HAL_ELEMENT_TYPE_FLOAT_32,
          // Encoding type:
          IREE_HAL_ENCODING_TYPE_DENSE_ROW_MAJOR,
          (iree_hal_buffer_params_t){
              // Where to allocate (host or device):
              .type = IREE_HAL_MEMORY_TYPE_DEVICE_LOCAL,
              // Access to allow to this memory:
              .access = IREE_HAL_MEMORY_ACCESS_ALL,
              // Intended usage of the buffer (transfers, dispatches, etc):
              .usage = IREE_HAL_BUFFER_USAGE_DEFAULT,
          },
          // The actual heap buffer to wrap or clone and its allocator:
          iree_make_const_byte_span(arg0_data, sizeof(arg0_data)),
          // Buffer view + storage are returned and owned by the caller:
          &arg0);
    }
    if (iree_status_is_ok(status)) {
      IREE_IGNORE_ERROR(iree_hal_buffer_view_fprint(
          stdout, arg0, /*max_element_count=*/4096, host_allocator));
      // Add to the call inputs list (which retains the buffer view).
      status = iree_runtime_call_inputs_push_back_buffer_view(&call, arg0);
    }
    // Since the call retains the buffer view we can release it here.
    iree_hal_buffer_view_release(arg0);

    fprintf(stdout, "\n * \n");

    // %arg1: tensor<4xf32>
    iree_hal_buffer_view_t* arg1 = NULL;
    if (iree_status_is_ok(status)) {
      static const iree_hal_dim_t arg1_shape[1] = {4};
      static const float arg1_data[4] = {10.0f, 100.0f, 1000.0f, 10000.0f};
      status = iree_hal_buffer_view_allocate_buffer_copy(
          device, device_allocator, IREE_ARRAYSIZE(arg1_shape), arg1_shape,
          IREE_HAL_ELEMENT_TYPE_FLOAT_32,
          IREE_HAL_ENCODING_TYPE_DENSE_ROW_MAJOR,
          (iree_hal_buffer_params_t){
              .type = IREE_HAL_MEMORY_TYPE_DEVICE_LOCAL,
              .access = IREE_HAL_MEMORY_ACCESS_ALL,
              .usage = IREE_HAL_BUFFER_USAGE_DEFAULT,
          },
          iree_make_const_byte_span(arg1_data, sizeof(arg1_data)), &arg1);
    }
    if (iree_status_is_ok(status)) {
      IREE_IGNORE_ERROR(iree_hal_buffer_view_fprint(
          stdout, arg1, /*max_element_count=*/4096, host_allocator));
      status = iree_runtime_call_inputs_push_back_buffer_view(&call, arg1);
    }
    iree_hal_buffer_view_release(arg1);
  }

}

void iree_jit_destroy(iree_jit_t* jit) {
  compiler_destroy(jit->compiler);
  free(jit);
}

