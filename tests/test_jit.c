#include "unity.h"
#include "jit.h"


void handle_compiler_error(iree_compiler_error_t *error) {
  const char *msg = ireeCompilerErrorGetMessage(error);
  fprintf(stderr, "Error from compiler API:\n%s\n", msg);
  ireeCompilerErrorDestroy(error);
}


void setUp(void) {
}

void tearDown(void) {
}

void test_here(void) {
    // Load the compiler library then initialize it.
    ireeCompilerLoadLibrary("libIREECompiler.so");
    ireeCompilerGlobalInitialize();

    // Create a session to track compiler state and set flags.
    iree_compiler_session_t *session = ireeCompilerSessionCreate();
    ireeCompilerSessionSetFlags(session, argc, argv);

    // Open a file as an input source to the compiler.
    const char *simple_mul_mlir = " \
        func.func @simple_mul(%lhs: tensor<4xf32>, %rhs: tensor<4xf32>) -> tensor<4xf32> {\n\
        %result = arith.mulf %lhs, %rhs : tensor<4xf32>\n \
        return %result : tensor<4xf32>\n \
        }";
    error = ireeCompilerSourceWrapBuffer(s.session, "simple_mul", simple_mul_mlir,
                                        strlen(simple_mul_mlir) + 1,
                                        /*isNullTerminated=*/true, &s.source);
    if (error) {
        fprintf(stderr, "Error wrapping source buffer\n");
        handle_compiler_error(error);
        return 1;
    }
    fprintf(stdout, "Wrapped simple_mul buffer as compiler source\n");

    // Use an invocation to compile from the input source to one or more outputs.
    iree_compiler_invocation_t *inv = ireeCompilerInvocationCreate(session);
    ireeCompilerInvocationPipeline(inv, IREE_COMPILER_PIPELINE_STD);

    // Output the compiled artifact to a file.
    iree_compiler_output_t *output = NULL;
    ireeCompilerOutputOpenFile("output.vmfb", &output);
    ireeCompilerInvocationOutputVMBytecode(inv, output);

    // Cleanup state.
    ireeCompilerInvocationDestroy(inv);
    ireeCompilerOutputDestroy(output);
    ireeCompilerSourceDestroy(source);
    ireeCompilerSessionDestroy(session);
    ireeCompilerGlobalShutdown();
}


int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_here);
    return UNITY_END();
}
