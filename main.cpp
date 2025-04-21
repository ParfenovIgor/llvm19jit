#include <llvm/IR/IRBuilder.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Transforms/IPO/ModuleInliner.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include "jit.h"

#include <memory>
#include <vector>

using namespace llvm;
using namespace std;

static std::unique_ptr<Module> jit_module = NULL;
static std::unique_ptr<LLVMContext> context;
static std::unique_ptr<IRBuilder<>> builder;

using LinkLayer = orc::RTDyldObjectLinkingLayer;
using Compiler = orc::SimpleCompiler;
using CompileLayer = orc::IRCompileLayer;

JITSymbol dummy_lookup(const string &name)
{
	return JITSymbol(NULL);
}

void code_gen()
{
	// (double, double, double)
	std::vector<Type *> param_type2(3, Type::getDoubleTy(*context));
	// double (*)(double, double, double)
	FunctionType *prototype2 = FunctionType::get(Type::getDoubleTy(*context), param_type2, false);

	Function *func2 = Function::Create(prototype2, Function::ExternalLinkage, "test_func2", jit_module.get());
	BasicBlock *body2 = BasicBlock::Create(*context, "body2", func2);
	builder->SetInsertPoint(body2);

	std::vector<Value *> args2;
	for (auto &arg : func2->args())
		args2.push_back(&arg);

	Value *temp2 = builder->CreateFAdd(args2[0], args2[1], "temp2");
	Value *ret2 = builder->CreateFAdd(args2[2], temp2, "result2");
	builder->CreateRet(ret2);

	// (double, double, double)
	std::vector<Type *> param_type(3, Type::getDoubleTy(*context));
	// double (*)(double, double, double)
	FunctionType *prototype = FunctionType::get(Type::getDoubleTy(*context), param_type, false);

	Function *func = Function::Create(prototype, Function::ExternalLinkage, "test_func", jit_module.get());
	BasicBlock *body = BasicBlock::Create(*context, "body", func);
	builder->SetInsertPoint(body);

	std::vector<Value *> args;
	for (auto &arg : func->args())
		args.push_back(&arg);

	Value *temp = CallInst::Create(func2, {args[0], args[1], args[2]}, "call", builder->GetInsertPoint());
	// Value* temp = builder->CreateFMul(args[0], args[1], "temp");
	Value *ret = builder->CreateFMul(args[2], temp, "result");
	builder->CreateRet(ret);
}

static ExitOnError ExitOnErr;
static std::unique_ptr<orc::KaleidoscopeJIT> TheJIT;

int main()
{
	// Initialization
	LLVMInitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();

	TheJIT = ExitOnErr(orc::KaleidoscopeJIT::Create());

	TargetMachine *target = EngineBuilder().selectTarget();
	context = std::make_unique<LLVMContext>();
	jit_module = std::make_unique<Module>("Test JIT Compiler", *context);
	jit_module->setDataLayout(TheJIT->getDataLayout());
	builder = std::make_unique<IRBuilder<>>(*context);

	// Emit the LLVM IR to the Module
	code_gen();
	
	// Create the analysis managers.
	// These must be declared in this order so that they are destroyed in the
	// correct order due to inter-analysis-manager references.
	LoopAnalysisManager LAM;
	FunctionAnalysisManager FAM;
	CGSCCAnalysisManager CGAM;
	ModuleAnalysisManager MAM;

	// Create the new pass manager builder.
	// Take a look at the PassBuilder constructor parameters for more
	// customization, e.g. specifying a TargetMachine or various debugging
	// options.
	PassBuilder PB;

	// Register all the basic analyses with the managers.
	PB.registerModuleAnalyses(MAM);
	PB.registerCGSCCAnalyses(CGAM);
	PB.registerFunctionAnalyses(FAM);
	PB.registerLoopAnalyses(LAM);
	PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

	// Create the pass manager.
	// This one corresponds to a typical -O2 optimization pipeline.
	ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);

	// Optimize the IR!
	MPM.run(*jit_module, MAM);

	jit_module->dump();

	auto TSM = orc::ThreadSafeModule(std::move(jit_module), std::move(context));
	ExitOnErr(TheJIT->addModule(std::move(TSM)));

	auto Sym = ExitOnErr(TheJIT->lookup("test_func"));

	auto *FP = Sym.getAddress().toPtr<double (*)(double, double, double)>();
	fprintf(stderr, "Evaluated to %f\n", FP(1.0, 2.0, 3.0));

	return 0;
}
