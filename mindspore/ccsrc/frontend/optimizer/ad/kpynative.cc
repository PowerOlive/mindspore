/**
 * This is the C++ adaptation and derivative work of Myia (https://github.com/mila-iqia/myia/).
 *
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>
#include "ir/anf.h"
#include "pipeline/jit/prim_bprop_optimizer.h"
#include "frontend/optimizer/ad/adjoint.h"
#include "frontend/optimizer/ad/dfunctor.h"
#include "frontend/optimizer/ad/kpynative.h"
#include "frontend/operator/ops.h"
#include "utils/info.h"
#include "debug/anf_ir_dump.h"
#include "debug/trace.h"

namespace mindspore {
namespace ad {
extern KPrim g_k_prims;

namespace {
FuncGraphPtr ZerosLikePrimOptPass(const pipeline::ResourcePtr &res) {
  static const opt::irpass::OptimizeIRPassLib irpass;
  opt::OptPassConfig eliminate_zeros_like_prim_pass = opt::OptPassConfig({
    irpass.zero_like_fill_zero_,
  });

  opt::OptPassGroupMap map({{"eliminate_zeros_like_prim_", eliminate_zeros_like_prim_pass}});

  auto eliminate_zeros_like_prim = opt::Optimizer::MakeOptimizer("eliminate_zeros_like_prim", res, map);
  FuncGraphPtr func_graph = res->func_graph();
  WITH(MsProfile::GetProfile()->Step("eliminate_zeros_like_prim"))[&eliminate_zeros_like_prim, &func_graph]() {
    func_graph = eliminate_zeros_like_prim->step(func_graph, true);
  };
  return func_graph;
}

FuncGraphPtr GetZerosLike(const abstract::AbstractBasePtrList &args_spec) {
  static ValuePtr zeros_like_ops = prim::GetPythonOps("zeros_like");
  static std::unordered_map<abstract::AbstractBasePtrList, FuncGraphPtr, abstract::AbstractBasePtrListHasher,
                            abstract::AbstractBasePtrListEqual>
    zeros_like_funcgraph_cache;
  auto iter = zeros_like_funcgraph_cache.find(args_spec);
  if (iter != zeros_like_funcgraph_cache.end()) {
    MS_LOG(DEBUG) << "Cache hit for zeros_like: " << mindspore::ToString(args_spec);
    return BasicClone(iter->second);
  }
  if (!zeros_like_ops->isa<MetaFuncGraph>()) {
    MS_LOG(EXCEPTION) << "zeros_like is not a MetaFuncGraph";
  }
  auto zeros_like = zeros_like_ops->cast<MetaFuncGraphPtr>();
  auto zeros_like_fg = zeros_like->GenerateFuncGraph(args_spec);
  MS_EXCEPTION_IF_NULL(zeros_like_fg);
  pipeline::ResourcePtr resource = std::make_shared<pipeline::Resource>();
  auto specialized_zeros_like_fg = pipeline::Renormalize(resource, zeros_like_fg, args_spec);
  MS_EXCEPTION_IF_NULL(specialized_zeros_like_fg);
  auto opted_zeros_like_fg = ZerosLikePrimOptPass(resource);
  MS_EXCEPTION_IF_NULL(opted_zeros_like_fg);
  zeros_like_funcgraph_cache[args_spec] = opted_zeros_like_fg;
  return BasicClone(opted_zeros_like_fg);
}

FuncGraphPtr GetHyperAdd(const abstract::AbstractBasePtrList &args_spec) {
  static ValuePtr add_ops = prim::GetPythonOps("hyper_add");
  static std::unordered_map<abstract::AbstractBasePtrList, FuncGraphPtr, abstract::AbstractBasePtrListHasher,
                            abstract::AbstractBasePtrListEqual>
    add_backward_funcgraph_cache;
  auto iter = add_backward_funcgraph_cache.find(args_spec);
  if (iter != add_backward_funcgraph_cache.end()) {
    MS_LOG(DEBUG) << "Cache hit for hyper_add: " << mindspore::ToString(args_spec);
    return BasicClone(iter->second);
  }
  if (!add_ops->isa<MetaFuncGraph>()) {
    MS_LOG(EXCEPTION) << "add is not a MetaFuncGraph";
  }
  auto add = add_ops->cast<MetaFuncGraphPtr>();
  auto add_fg = add->GenerateFuncGraph(args_spec);
  MS_EXCEPTION_IF_NULL(add_fg);
  pipeline::ResourcePtr resource = std::make_shared<pipeline::Resource>();
  auto specialized_add_fg = pipeline::Renormalize(resource, add_fg, args_spec);
  MS_EXCEPTION_IF_NULL(specialized_add_fg);
  add_backward_funcgraph_cache[args_spec] = specialized_add_fg;
  return BasicClone(specialized_add_fg);
}

AnfNodePtr BuildZerosLikeNode(const FuncGraphPtr &tape, const AnfNodePtr &node) {
  // Build zeros_like(node) as dout
  abstract::AbstractBasePtrList args_spec{node->abstract()->Broaden()};
  auto zeros_like_fg = GetZerosLike(args_spec);
  auto zeros_like_node = tape->NewCNode({NewValueNode(zeros_like_fg), node});
  zeros_like_node->set_abstract(zeros_like_fg->output()->abstract());
  return zeros_like_node;
}

AnfNodePtr BuildZerosLikeValue(const FuncGraphPtr &tape, const ValuePtr &out) {
  // Build zeros_like(out) as dout
  abstract::AbstractBasePtrList args_spec{out->ToAbstract()->Broaden()};
  auto zeros_like_fg = GetZerosLike(args_spec);
  auto zeros_like_value = tape->NewCNode({NewValueNode(zeros_like_fg), NewValueNode(out)});
  zeros_like_value->set_abstract(zeros_like_fg->output()->abstract());
  return zeros_like_value;
}

FuncGraphPtr GetOnesLike(const abstract::AbstractBasePtrList &args_spec) {
  static ValuePtr ones_like_ops = prim::GetPythonOps("ones_like");
  static std::unordered_map<abstract::AbstractBasePtrList, FuncGraphPtr, abstract::AbstractBasePtrListHasher,
                            abstract::AbstractBasePtrListEqual>
    ones_like_funcgraph_cache;
  auto iter = ones_like_funcgraph_cache.find(args_spec);
  if (iter != ones_like_funcgraph_cache.end()) {
    MS_LOG(DEBUG) << "Cache hit for ones_like: " << mindspore::ToString(args_spec);
    return BasicClone(iter->second);
  }
  if (!ones_like_ops->isa<MetaFuncGraph>()) {
    MS_LOG(EXCEPTION) << "ones_like is not a MetaFuncGraph";
  }
  auto ones_like = ones_like_ops->cast<MetaFuncGraphPtr>();
  auto ones_like_fg = ones_like->GenerateFuncGraph(args_spec);
  MS_EXCEPTION_IF_NULL(ones_like_fg);
  pipeline::ResourcePtr resource = std::make_shared<pipeline::Resource>();
  auto specialized_ones_like_fg = pipeline::Renormalize(resource, ones_like_fg, args_spec);
  MS_EXCEPTION_IF_NULL(specialized_ones_like_fg);
  ones_like_funcgraph_cache[args_spec] = specialized_ones_like_fg;
  return BasicClone(specialized_ones_like_fg);
}

AnfNodePtr BuildOnesLikeValue(const FuncGraphPtr &tape, const ValuePtr &out) {
  // Build ones_like(out) as dout
  abstract::AbstractBasePtrList args_spec{out->ToAbstract()->Broaden()};
  auto ones_like_fg = GetOnesLike(args_spec);
  auto ones_like_value = tape->NewCNode({NewValueNode(ones_like_fg), NewValueNode(out)});
  ones_like_value->set_abstract(ones_like_fg->output()->abstract());
  return ones_like_value;
}

// This Faked BProp func_graph should not be present in the final top bprop func_graph.
FuncGraphPtr BuildFakeBProp(const PrimitivePtr &prim, size_t inputs_num) {
  auto func_graph = std::make_shared<FuncGraph>();
  std::vector<AnfNodePtr> outputs;
  outputs.push_back(NewValueNode(prim::kPrimMakeTuple));

  auto fake_bprop = std::make_shared<Primitive>("fake_bprop");
  (void)fake_bprop->AddAttr("info", MakeValue("Primitive " + prim->name() + "'s bprop not defined."));
  auto fake_input_sens = func_graph->NewCNode({NewValueNode(fake_bprop), NewValueNode(true)});

  for (size_t i = 0; i < inputs_num; ++i) {
    // Mock params for inputs
    auto param = func_graph->add_parameter();
    // Mock derivatives for each inputs
    outputs.push_back(fake_input_sens);
  }
  // mock params for out and dout
  (void)func_graph->add_parameter();
  (void)func_graph->add_parameter();
  func_graph->set_output(func_graph->NewCNode(outputs));
  return func_graph;
}
}  // namespace

class PynativeAdjoint {
 public:
  PynativeAdjoint(const FuncGraphPtr &tape, const ValuePtrList &op_args, const ValuePtr &out,
                  const FuncGraphPtr &bprop_fg)
      : tape_(tape), op_args_(op_args), out_(out), bprop_fg_(bprop_fg) {}

  AnfNodePtrList &users() { return users_; }
  const ValuePtrList &op_args() { return op_args_; }
  const ValuePtr &out() { return out_; }
  const FuncGraphPtr &bprop_fg() { return bprop_fg_; }
  AnfNodePtr RealDout() {
    if (dout_ != nullptr) {
      return dout_;
    }
    return BuildZerosLikeValue(tape_, out_);
  }

  void AccumulateDout(const AnfNodePtr &dout_factor) {
    if (dout_factor->abstract() == nullptr) {
      MS_LOG(EXCEPTION) << "Abstract of dout_factor should not be null" << dout_factor->ToString();
    }
    if (dout_ != nullptr) {
      MS_LOG(DEBUG) << "Update dout " << dout_->ToString() << " with dout_factor " << dout_factor->ToString();
      auto arg = out_->ToAbstract()->Broaden();
      abstract::AbstractBasePtrList args_spec{arg, arg};
      auto add_fg = GetHyperAdd(args_spec);
      MS_EXCEPTION_IF_NULL(add_fg);
      dout_ = tape_->NewCNode({NewValueNode(add_fg), dout_, dout_factor});
      dout_->set_abstract(add_fg->output()->abstract());
      MS_LOG(DEBUG) << "New dout_ " << dout_->DebugString();
      return;
    }
    dout_ = dout_factor;
  }

 private:
  const FuncGraphPtr tape_;
  AnfNodePtr dout_{nullptr};
  AnfNodePtrList users_;
  // cache these arguments from ad caller.
  const ValuePtrList op_args_;
  // For CNode , it's output of cnode. For Parameter or ValueNode, it's its value.
  const ValuePtr out_;
  // bprop_fg passed from ad caller, it may be user defined back propagate funcgragh.
  const FuncGraphPtr bprop_fg_;
};
using PynativeAdjointPtr = std::shared_ptr<PynativeAdjoint>;

class KPynativeCellImpl : public KPynativeCell {
 public:
  explicit KPynativeCellImpl(const AnfNodePtrList &cell_inputs)
      : tape_(std::make_shared<FuncGraph>()), cell_inputs_(cell_inputs) {
    tape_->debug_info()->set_name("grad_top");
    for (size_t i = 0; i < cell_inputs.size(); ++i) {
      TraceGuard trace_guard(std::make_shared<TraceCopy>(cell_inputs[i]->debug_info()));
      tape_->add_parameter();
    }
  }
  ~KPynativeCellImpl() override = default;
  bool KPynativeOp(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out);
  bool KPynativeWithBProp(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out,
                          const FuncGraphPtr &bprop_fg);
  void UpdateOutputNodeOfTopCell(const AnfNodePtr &output_node) override;
  FuncGraphPtr Finish(const AnfNodePtrList &weights, bool grad_inputs, bool grad_weights, bool has_sens_arg);

 private:
  FuncGraphPtr tape_;
  OrderedMap<AnfNodePtr, PynativeAdjointPtr> anfnode_to_adjoin_;
  AnfNodePtrList cell_inputs_;
  // Last cnode of this Cell, may be a primitive op or cell with user defined bprop.
  AnfNodePtr last_node_{nullptr};
  bool need_propagate_stop_gradient_{false};

  // For CNode like TupleGetItem, ListGetItem, MakeTuple, MakeList, it's bypassed by caller so
  // no KPynativeOp is called for these CNode. Here we forge Adjoint for these CNode.
  PynativeAdjointPtr ForgeCNodeAdjoint(const CNodePtr &cnode);
  PynativeAdjointPtr ForgeGetItemAdjoint(const CNodePtr &cnode);
  PynativeAdjointPtr ForgeMakeSequenceAdjoint(const CNodePtr &cnode);
  bool BuildAdjoint(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out,
                    const FuncGraphPtr &bprop_fg);
  void PropagateStopGradient();
  bool AllReferencesStopped(const CNodePtr &curr_cnode);
  // Back propagate for all node;
  bool BackPropagate();
  bool BackPropagate(const CNodePtr &cnode_primal, const CNodePtr &bprop_app);
  FuncGraphPtr BuildBpropCutFuncGraph(const PrimitivePtr &prim, const CNodePtr &cnode);
  // Back propagate for MakeList or MakeTuple is generated from MetaFuncGraph.
  FuncGraphPtr BuildMakeSequenceBprop(const PrimitivePtr &prim, const CNodePtr &cnode);
  // Set return node according to grad flag
  void SetOutput(const AnfNodePtrList &weights, bool grad_inputs, bool grad_weights);
};
using KPynativeCellImplPtr = std::shared_ptr<KPynativeCellImpl>;

KPynativeCellPtr GradPynativeCellBegin(const AnfNodePtrList &cell_inputs) {
  auto abstract_are_set = std::all_of(cell_inputs.cbegin(), cell_inputs.cend(),
                                      [](const AnfNodePtr &node) { return node->abstract() != nullptr; });
  if (!abstract_are_set) {
    MS_LOG(EXCEPTION) << "Not all abstract_value in cell_inputs are set";
  }
  return std::make_shared<KPynativeCellImpl>(cell_inputs);
}

FuncGraphPtr GradPynativeCellEnd(const KPynativeCellPtr &k_cell, const AnfNodePtrList &weights, bool grad_inputs,
                                 bool grad_weights, bool has_sens_arg) {
  auto k_cell_impl = std::dynamic_pointer_cast<KPynativeCellImpl>(k_cell);
  return k_cell_impl->Finish(weights, grad_inputs, grad_weights, has_sens_arg);
}

FuncGraphPtr KPynativeCellImpl::Finish(const AnfNodePtrList &weights, bool grad_inputs, bool grad_weights,
                                       bool has_sens_arg) {
  // propagate stop_gradient flag to cnode before back propagate;
  PropagateStopGradient();
  MS_LOG(DEBUG) << "Last node info " << last_node_->DebugString();
  auto last_node_adjoint_iter = anfnode_to_adjoin_.find(last_node_);
  if (last_node_adjoint_iter == anfnode_to_adjoin_.end()) {
    MS_LOG(EXCEPTION) << "BackPropagate adjoint does not exist for input: " << last_node_->ToString();
  }
  if (has_sens_arg) {
    // sens parameter;
    auto sens_param = tape_->add_parameter();
    sens_param->debug_info()->set_name("sens");
    sens_param->set_abstract(last_node_adjoint_iter->second->out()->ToAbstract()->Broaden());
    // Set dout of last node to sens;
    last_node_adjoint_iter->second->AccumulateDout(sens_param);
  } else {
    auto sens_node = BuildOnesLikeValue(tape_, last_node_adjoint_iter->second->out());
    last_node_adjoint_iter->second->AccumulateDout(sens_node);
  }
  // Add weights parameter
  for (const auto &weight : weights) {
    TraceGuard trace_guard(std::make_shared<TraceCopy>(weight->debug_info()));
    auto p = tape_->add_parameter();
    auto input_w = weight->cast<ParameterPtr>();
    MS_EXCEPTION_IF_NULL(input_w);
    p->set_default_param(input_w->default_param());
  }

  // BackPropagate sensitivity;
  BackPropagate();
  // Return the gradient;
  SetOutput(weights, grad_inputs, grad_weights);
  // Replace AnfNode with parameter of tape_;
  auto mng = MakeManager({tape_}, false);
  auto tr = mng->Transact();
  const auto &parameters = tape_->parameters();
  auto cell_inputs_size = cell_inputs_.size();
  for (size_t i = 0; i < cell_inputs_size; ++i) {
    tr.Replace(cell_inputs_[i], parameters[i]);
  }
  // (Inputs, sens, weights) or (Inputs, weights)
  size_t weight_offset = cell_inputs_size;
  if (has_sens_arg) {
    weight_offset = weight_offset + 1;
  }
  for (size_t i = 0; i < weights.size(); ++i) {
    tr.Replace(weights[i], parameters[weight_offset + i]);
  }
  tr.Commit();

  if (MsContext::GetInstance()->get_param<bool>(MS_CTX_SAVE_GRAPHS_FLAG)) {
    DumpIR("before_final_opt.ir", tape_);
  }
  return tape_;
}

bool GradPynativeOp(const KPynativeCellPtr &k_cell, const CNodePtr &cnode, const ValuePtrList &op_args,
                    const ValuePtr &out) {
  auto k_cell_impl = std::dynamic_pointer_cast<KPynativeCellImpl>(k_cell);
  return k_cell_impl->KPynativeOp(cnode, op_args, out);
}

bool KPynativeCellImpl::KPynativeOp(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out) {
  MS_EXCEPTION_IF_NULL(cnode);
  auto prim = GetCNodePrimitive(cnode);
  if (prim == nullptr) {
    MS_LOG(EXCEPTION) << "Should be primitive, but: " << cnode->DebugString();
  }
  if (IsPrimitiveEquals(prim, prim::kPrimStopGradient) || IsPrimitiveEquals(prim, prim::kPrimUpdateState)) {
    need_propagate_stop_gradient_ = true;
  }

  FuncGraphPtr bprop_fg = nullptr;
  if (IsPrimitiveEquals(prim, prim::kPrimHookBackward)) {
    bprop_fg = BuildBpropCutFuncGraph(prim, cnode);
  } else if (IsPrimitiveEquals(prim, prim::kPrimMakeTuple) || IsPrimitiveEquals(prim, prim::kPrimMakeList)) {
    bprop_fg = BuildMakeSequenceBprop(prim, cnode);
  } else {
    bprop_fg = g_k_prims.GetPossibleBprop(prim);
    if (bprop_fg == nullptr) {
      MS_LOG(DEBUG) << "Cannot find defined bprop for cnode prim: " << cnode->DebugString();
      bprop_fg = BuildFakeBProp(prim, cnode->size() - 1);
    }
  }
  MS_EXCEPTION_IF_NULL(bprop_fg);
  BuildAdjoint(cnode, op_args, out, bprop_fg);

  return true;
}

bool GradPynativeWithBProp(const KPynativeCellPtr &k_cell, const CNodePtr &cnode, const ValuePtrList &op_args,
                           const ValuePtr &out, const FuncGraphPtr &bprop_fg) {
  auto k_cell_impl = std::dynamic_pointer_cast<KPynativeCellImpl>(k_cell);
  return k_cell_impl->KPynativeWithBProp(cnode, op_args, out, bprop_fg);
}

bool KPynativeCellImpl::KPynativeWithBProp(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out,
                                           const FuncGraphPtr &bprop_fg) {
  MS_EXCEPTION_IF_NULL(cnode);
  auto primal_fg = GetCNodeFuncGraph(cnode);
  if (primal_fg == nullptr) {
    MS_LOG(EXCEPTION) << "Should be func graph, but: " << cnode->DebugString();
  }
  MS_EXCEPTION_IF_NULL(bprop_fg);
  BuildAdjoint(cnode, op_args, out, bprop_fg);

  return true;
}

void KPynativeCellImpl::UpdateOutputNodeOfTopCell(const AnfNodePtr &output_node) {
  MS_EXCEPTION_IF_NULL(output_node);
  MS_LOG(DEBUG) << "Real output node of top cell is " << output_node->DebugString();
  last_node_ = output_node;

  auto last_node_adjoint_iter = anfnode_to_adjoin_.find(last_node_);
  if (last_node_adjoint_iter == anfnode_to_adjoin_.end()) {
    if (IsPrimitiveCNode(output_node, prim::kPrimTupleGetItem) || IsPrimitiveCNode(output_node, prim::kPrimListGetItem)) {
      MS_LOG(DEBUG) << "Build cnode adjoint for anfnode: " << output_node->DebugString();
      auto cnode = output_node->cast<CNodePtr>();
      (void)ForgeGetItemAdjoint(cnode);
      return;
    }
    MS_LOG (EXCEPTION) << "BackPropagate adjoint does not exist for input: " << last_node_->ToString();
  }
}

namespace {
ValuePtr ShallowCopyValue(const ValuePtr &value) {
  if (value->isa<mindspore::tensor::Tensor>()) {
    auto tensor_value = value->cast<mindspore::tensor::TensorPtr>();
    return std::make_shared<mindspore::tensor::Tensor>(*tensor_value);
  } else if (value->isa<ValueTuple>()) {
    std::vector<ValuePtr> values;
    auto value_tuple = value->cast<ValueTuplePtr>();
    std::transform(value_tuple->value().begin(), value_tuple->value().end(), std::back_inserter(values),
                   [](const ValuePtr &elem) { return ShallowCopyValue(elem); });
    return std::make_shared<ValueTuple>(values);
  } else {
    return value;
  }
}
}  // namespace

PynativeAdjointPtr KPynativeCellImpl::ForgeGetItemAdjoint(const CNodePtr &cnode) {
  if (cnode->size() != 3) {
    MS_LOG(EXCEPTION) << "TupleGetItem/ListGetItem CNode should have 3 inputs, but CNode: " << cnode->DebugString();
  }
  // Input 1 of CNode;
  PynativeAdjointPtr inp_1_adjoint = nullptr;
  auto inp_1 = cnode->input(1);
  auto inp_1_adjoint_iter = anfnode_to_adjoin_.find(inp_1);
  if (inp_1_adjoint_iter == anfnode_to_adjoin_.end()) {
    if (!inp_1->isa<CNode>()) {
      MS_LOG(EXCEPTION) << "Input 1 of CNode should be a CNode, CNode: " << cnode->DebugString();
    }
    inp_1_adjoint = ForgeCNodeAdjoint(inp_1->cast<CNodePtr>());
    if (inp_1_adjoint == nullptr) {
      MS_LOG(EXCEPTION) << "Build adjoint for input 1 of CNode failed, CNode: " << cnode->DebugString();
    }
    inp_1_adjoint->users().push_back(cnode);
  } else {
    inp_1_adjoint = inp_1_adjoint_iter->second;
  }
  if (!inp_1_adjoint->out()->isa<ValueSequeue>()) {
    MS_LOG(EXCEPTION) << "Input of CNode should be evaluated to a ValueSequence. CNode: " << cnode->DebugString()
                      << ", out of input1: " << inp_1_adjoint->out();
  }
  auto inp_1_out = inp_1_adjoint->out()->cast<ValueSequeuePtr>();

  // Input 2 of CNode;
  auto index_value = GetValueNode<Int64ImmPtr>(cnode->input(2));
  if (index_value == nullptr) {
    MS_LOG(EXCEPTION) << "CNode input 2 should be a Int64Imm, CNode: " << cnode->DebugString();
  }
  if (index_value->value() < 0) {
    MS_LOG(EXCEPTION) << "CNode input 2 should not less than 0, CNode: " << cnode->DebugString();
  }
  size_t index_value_imm = index_value->value();
  if (index_value_imm >= inp_1_out->size()) {
    MS_LOG(EXCEPTION) << "CNode input 2 should be index between [0, " << inp_1_out->size()
                      << ", but: " << index_value->ToString();
  }
  auto cnode_out = (*inp_1_out)[index_value_imm];
  ValuePtrList op_args{inp_1_out, index_value};
  auto built = KPynativeOp(cnode, op_args, cnode_out);
  if (!built) {
    MS_LOG(EXCEPTION) << "Build Adjoint for GetItem node failed, CNode: " << cnode->DebugString();
  }
  auto cnode_adjoint_iter = anfnode_to_adjoin_.find(cnode);
  if (cnode_adjoint_iter == anfnode_to_adjoin_.end()) {
    MS_LOG(EXCEPTION) << "Build Adjoint for GetItem node failed, CNode: " << cnode->DebugString();
  }
  return cnode_adjoint_iter->second;
}

PynativeAdjointPtr KPynativeCellImpl::ForgeMakeSequenceAdjoint(const CNodePtr &cnode) {
  // () or [] is not supported yet.
  if (cnode->size() <= 1) {
    MS_LOG(DEBUG) << "MakeTuple/MakeList CNode is empty Tuple/List, CNode: " << cnode->DebugString();
    static auto dummy_adjoint = std::make_shared<PynativeAdjoint>(nullptr, ValuePtrList{}, nullptr, nullptr);
    anfnode_to_adjoin_[cnode] = dummy_adjoint;
    cnode->set_stop_gradient(true);
    return dummy_adjoint;
  }
  ValuePtrList op_args;
  for (size_t i = 1; i < cnode->size(); ++i) {
    const auto &inp = cnode->input(i);
    auto inp_adjoint_iter = anfnode_to_adjoin_.find(inp);
    if (inp_adjoint_iter == anfnode_to_adjoin_.end()) {
      MS_LOG(DEBUG) << "Item in CNode cannot found in cache. Inp is: " << inp->DebugString();
      if (inp->isa<CNode>()) {
        const auto inp_cnode = inp->cast<CNodePtr>();
        MS_EXCEPTION_IF_NULL(inp_cnode);
        auto forged_inp_adjoint = ForgeCNodeAdjoint(inp->cast<CNodePtr>());
        op_args.push_back(forged_inp_adjoint->out());
      } else if (inp->isa<ValueNode>()) {
        const auto &inp_value = GetValueNode(inp);
        op_args.push_back(inp_value);
      } else {
        MS_LOG(EXCEPTION) << "Input of MakeTuple/MakeLis is not a CNode or ValueNode, but: " << inp->DebugString();
      }
    } else {
      op_args.push_back(inp_adjoint_iter->second->out());
    }
  }
  ValuePtr cnode_out = nullptr;
  if (IsPrimitiveCNode(cnode, prim::kPrimMakeTuple)) {
    cnode_out = MakeValue(op_args);
  }
  if (IsPrimitiveCNode(cnode, prim::kPrimMakeList)) {
    cnode_out = std::make_shared<ValueList>(op_args);
  }

  auto built = KPynativeOp(cnode, op_args, cnode_out);
  if (!built) {
    MS_LOG(EXCEPTION) << "Build Adjoint for MakeTuple/MakeList node failed, CNode: " << cnode->DebugString();
  }
  auto cnode_adjoint_iter = anfnode_to_adjoin_.find(cnode);
  if (cnode_adjoint_iter == anfnode_to_adjoin_.end()) {
    MS_LOG(EXCEPTION) << "Build Adjoint for MakeTuple/MakeList node failed, CNode: " << cnode->DebugString();
  }
  return cnode_adjoint_iter->second;
}

PynativeAdjointPtr KPynativeCellImpl::ForgeCNodeAdjoint(const CNodePtr &cnode) {
  if (IsPrimitiveCNode(cnode, prim::kPrimTupleGetItem) || IsPrimitiveCNode(cnode, prim::kPrimListGetItem)) {
    MS_LOG(DEBUG) << "Build cnode adjoint for anfnode: " << cnode->DebugString();
    return ForgeGetItemAdjoint(cnode);
  }

  if (IsPrimitiveCNode(cnode, prim::kPrimMakeTuple) || IsPrimitiveCNode(cnode, prim::kPrimMakeList)) {
    MS_LOG(DEBUG) << "Build cnode adjoint for anfnode: " << cnode->DebugString();
    return ForgeMakeSequenceAdjoint(cnode);
  }
  MS_LOG(EXCEPTION) << "Unknown cnode: " << cnode->DebugString();
}

bool KPynativeCellImpl::BuildAdjoint(const CNodePtr &cnode, const ValuePtrList &op_args, const ValuePtr &out,
                                     const FuncGraphPtr &bprop_fg) {
  // Optimize the bprop_fg based on value.
  // Clone op_args and out, so the address of tensor data can be reset to nullptr if the value of tensor
  // is not used in bprop_fg;
  ValuePtrList cloned_op_args;
  std::transform(op_args.begin(), op_args.end(), std::back_inserter(cloned_op_args),
                 [](const ValuePtr &value) { return ShallowCopyValue(value); });
  ValuePtr cloned_out = ShallowCopyValue(out);
  auto optimized_bprop_fg = OptimizeBPropFuncGraph(bprop_fg, cnode, cloned_op_args, cloned_out);

  auto anfnode_adjoint_iter = anfnode_to_adjoin_.find(cnode);
  if (anfnode_adjoint_iter != anfnode_to_adjoin_.end()) {
    MS_LOG(EXCEPTION) << "CNode should be unique, but: " << cnode->DebugString();
  }
  // Book-keeping last cnode, as dout of this node will be given from outside;
  last_node_ = cnode;

  for (size_t i = 1; i < cnode->inputs().size(); ++i) {
    auto inp_i = cnode->input(i);
    auto input_anfnode_adjoint_iter = anfnode_to_adjoin_.find(inp_i);
    if (input_anfnode_adjoint_iter == anfnode_to_adjoin_.end()) {
      if (inp_i->isa<CNode>()) {
        auto cnode_inp_i = inp_i->cast<CNodePtr>();
        auto forged_adjoint = ForgeCNodeAdjoint(cnode_inp_i);
        if (forged_adjoint == nullptr) {
          MS_LOG(EXCEPTION) << "Cannot forge adjoint for anfnode: " << inp_i->DebugString();
        }
        forged_adjoint->users().push_back(cnode);
      } else {
        auto inp_i_pynative_adjoint = std::make_shared<PynativeAdjoint>(tape_, ValuePtrList{}, op_args[i - 1], nullptr);
        anfnode_to_adjoin_.insert(std::make_pair(inp_i, inp_i_pynative_adjoint));
        inp_i_pynative_adjoint->users().push_back(cnode);
      }
    } else {
      input_anfnode_adjoint_iter->second->users().push_back(cnode);
    }
  }

  auto cnode_pynative_adjoint =
    std::make_shared<PynativeAdjoint>(tape_, cloned_op_args, cloned_out, optimized_bprop_fg);
  anfnode_to_adjoin_.insert(std::make_pair(cnode, cnode_pynative_adjoint));

  return true;
}

FuncGraphPtr OptimizeBPropFuncGraph(const FuncGraphPtr &bprop_fg, const CNodePtr &cnode, const ValuePtrList &op_args,
                                    const ValuePtr &out) {
  auto optimized_bprop_fg =
    pipeline::PrimBpropOptimizer::GetPrimBpropOptimizerInst().OptimizeBPropFuncGraph(bprop_fg, cnode, op_args, out);
  return optimized_bprop_fg;
}

bool KPynativeCellImpl::BackPropagate(const CNodePtr &cnode_primal, const CNodePtr &bprop_app) {
  abstract::AbstractTuplePtr abstract_tuple = nullptr;
  auto bprop_app_abstract = bprop_app->abstract();
  abstract_tuple = bprop_app_abstract->cast<abstract::AbstractTuplePtr>();
  if (abstract_tuple->size() != (cnode_primal->size() - 1)) {
    MS_LOG(EXCEPTION) << "AbstractTuple size: " << abstract_tuple->ToString()
                      << " not match primal cnode input size: " << cnode_primal->DebugString();
  }
  for (size_t i = 1; i < cnode_primal->size(); i++) {
    auto input = cnode_primal->input(i);
    // Useless to accumulate sens for ValueNode, the sens for ValueNode should be zeros_like;
    if (input->isa<ValueNode>()) {
      continue;
    }
    auto cnode_input = input->cast<CNodePtr>();
    if (cnode_input != nullptr && cnode_input->stop_gradient()) {
      MS_LOG(DEBUG) << "Bypass accumulate dout to cnode with stop_gradient flag, cnode: " << input->ToString();
      continue;
    }
    // Backprop sens wrt inputs.
    auto input_adjoint_iter = anfnode_to_adjoin_.find(input);
    if (input_adjoint_iter == anfnode_to_adjoin_.end()) {
      MS_LOG(EXCEPTION) << "BackPropagate adjoint does not exist input[" << i << "] " << input->ToString();
    }
    auto din = tape_->NewCNode({NewValueNode(prim::kPrimTupleGetItem), bprop_app, NewValueNode(SizeToLong(i - 1))});
    if (abstract_tuple != nullptr) {
      din->set_abstract((*abstract_tuple)[i - 1]);
    }
    input_adjoint_iter->second->AccumulateDout(din);
  }
  return true;
}

bool KPynativeCellImpl::BackPropagate() {
  for (auto iter = anfnode_to_adjoin_.rbegin(); iter != anfnode_to_adjoin_.rend(); ++iter) {
    if (!iter->first->isa<CNode>()) {
      continue;
    }
    auto cnode = iter->first->cast<CNodePtr>();
    if (cnode->stop_gradient()) {
      MS_LOG(DEBUG) << "Bypass backpropagate for cnode with stop_gradient flag: " << cnode->ToString();
      continue;
    }
    MS_LOG(DEBUG) << "BackPropagate for CNode: " << cnode->ToString();
    auto bprop_fg = iter->second->bprop_fg();
    MS_EXCEPTION_IF_NULL(bprop_fg);

    AnfNodePtrList node_list{NewValueNode(bprop_fg)};
    std::transform(iter->second->op_args().begin(), iter->second->op_args().end(), std::back_inserter(node_list),
                   [](const ValuePtr &value) { return NewValueNode(value); });
    node_list.push_back(NewValueNode(iter->second->out()));
    node_list.push_back(iter->second->RealDout());
    // Update abstract info of valuenode with its value
    for (size_t i = 1; i < node_list.size() - 1; ++i) {
      auto v_node = node_list[i]->cast<ValueNodePtr>();
      MS_EXCEPTION_IF_NULL(v_node);
      auto value = v_node->value();
      if (v_node->abstract() == nullptr && value != nullptr && value->ToAbstract() != nullptr) {
        v_node->set_abstract(value->ToAbstract()->Broaden());
      }
    }
    // Back propagate process
    auto bprop_fg_output_abs = bprop_fg->output()->abstract();
    if (bprop_fg_output_abs == nullptr || !bprop_fg_output_abs->isa<abstract::AbstractTuple>()) {
      if (bprop_fg_output_abs == nullptr) {
        MS_LOG(WARNING) << "Abstract of bprop_fg_output_abs is not AbstractTuple, but nullptr";
      } else {
        MS_LOG(WARNING) << "Abstract of bprop_fg_output_abs is not AbstractTuple, but: "
                        << bprop_fg_output_abs->ToString();
      }
    }
    auto bprop_app = tape_->NewCNode(node_list);
    bprop_app->set_abstract(bprop_fg_output_abs);
    BackPropagate(cnode, bprop_app);
  }
  return true;
}

bool KPynativeCellImpl::AllReferencesStopped(const CNodePtr &curr_cnode) {
  // If all CNode use curr_cnode has stop_gradient_ flag, then curr_cnode also can set that flag.
  auto iter = anfnode_to_adjoin_.find(curr_cnode);
  if (iter == anfnode_to_adjoin_.end()) {
    MS_LOG(EXCEPTION) << "Cannot find adjoint for cnode: " << curr_cnode->DebugString();
  }
  auto users = iter->second->users();
  if (users.empty()) {
    return false;
  }
  auto all_users_have_stopped = std::all_of(users.cbegin(), users.cend(), [](const AnfNodePtr &user) {
    if (!user->isa<CNode>() || !user->cast<CNodePtr>()->stop_gradient()) {
      return false;
    }
    return true;
  });
  return all_users_have_stopped;
}

void KPynativeCellImpl::PropagateStopGradient() {
  // propagate need_stop_gradient_ to cnode before back propagate;
  if (need_propagate_stop_gradient_) {
    for (auto iter = anfnode_to_adjoin_.rbegin(); iter != anfnode_to_adjoin_.rend(); ++iter) {
      const auto &node = iter->first;
      if (node->isa<CNode>()) {
        auto cnode = node->cast<CNodePtr>();
        if (!cnode->stop_gradient()) {
          // Cut off the cnode only when it's not referred any more
          if (IsPrimitiveCNode(cnode, prim::kPrimStopGradient) || IsPrimitiveCNode(cnode, prim::kPrimUpdateState) ||
              AllReferencesStopped(cnode)) {
            MS_LOG(DEBUG) << "Set stop_gradient flag for " << cnode->ToString();
            cnode->set_stop_gradient(true);
          }
        }
      }
    }
  }
}

FuncGraphPtr KPynativeCellImpl::BuildBpropCutFuncGraph(const PrimitivePtr &prim, const CNodePtr &cnode) {
  auto inputs_num = cnode->size() - 1;

  auto func_graph = std::make_shared<FuncGraph>();
  std::vector<AnfNodePtr> outputs;

  auto bprop_cut = std::make_shared<PrimitivePy>("bprop_cut", py::object());
  bprop_cut->CopyHookFunction(prim);

  auto cell_id = GetValue<std::string>(prim->GetAttr("cell_id"));
  if (cell_id != "") {
    (void)bprop_cut->AddAttr("cell_hook", MakeValue(true));
    (void)bprop_cut->AddAttr("cell_id", MakeValue(cell_id));
  }

  outputs.push_back(NewValueNode(bprop_cut));
  for (size_t i = 0; i < inputs_num; ++i) {
    auto param = func_graph->add_parameter();
    outputs.push_back(param);
  }
  // out, dout
  auto p1 = func_graph->add_parameter();
  auto p2 = func_graph->add_parameter();
  outputs.push_back(p1);
  outputs.push_back(p2);

  func_graph->set_output(func_graph->NewCNode(outputs));
  return func_graph;
}

FuncGraphPtr KPynativeCellImpl::BuildMakeSequenceBprop(const PrimitivePtr &prim, const CNodePtr &cnode) {
  using CacheKey = std::pair<std::string, size_t>;
  static std::map<CacheKey, FuncGraphPtr> bprop_func_graph_cache;
  auto inputs_num = cnode->size() - 1;
  CacheKey key{prim->name(), inputs_num};
  auto bprop_func_graph_iter = bprop_func_graph_cache.find(key);
  if (bprop_func_graph_iter != bprop_func_graph_cache.end()) {
    return bprop_func_graph_iter->second;
  }

  FuncGraphPtr b = std::make_shared<FuncGraph>();

  std::ostringstream ss;
  ss << "◀" << prim->ToString() << inputs_num;
  b->debug_info()->set_name(ss.str());
  for (size_t i = 0; i < inputs_num; ++i) {
    auto param = b->add_parameter();
  }
  // out, dout
  auto p1 = b->add_parameter();
  MS_EXCEPTION_IF_NULL(p1);
  AnfNodePtr dout = b->add_parameter();

  std::vector<AnfNodePtr> grads;
  PrimitivePtr getitem_prim;

  if (IsPrimitiveEquals(prim, prim::kPrimMakeTuple)) {
    getitem_prim = prim::kPrimTupleGetItem;
  } else if (IsPrimitiveEquals(prim, prim::kPrimMakeList)) {
    getitem_prim = prim::kPrimListGetItem;
  } else {
    MS_LOG(EXCEPTION) << "Prim should be MakeTuple or MakeList, Invalid prim: " << prim->ToString();
  }

  grads.push_back(NewValueNode(prim));
  for (size_t i = 0; i < inputs_num; ++i) {
    grads.push_back(b->NewCNode({NewValueNode(getitem_prim), dout, NewValueNode(SizeToLong(i))}));
  }

  b->set_flag(FUNC_GRAPH_FLAG_CORE, true);
  b->set_output(b->NewCNode(grads));

  bprop_func_graph_cache[key] = b;
  return b;
}

void KPynativeCellImpl::SetOutput(const AnfNodePtrList &weights, bool grad_inputs, bool grad_weights) {
  AnfNodePtrList grad_inputs_list{NewValueNode(prim::kPrimMakeTuple)};
  AbstractBasePtr grad_inputs_spec;
  if (grad_inputs) {
    AbstractBasePtrList grad_inputs_abs_list;
    for (const auto &input : cell_inputs_) {
      MS_EXCEPTION_IF_NULL(input);
      auto input_adjoint_iter = anfnode_to_adjoin_.find(input);
      if (input_adjoint_iter == anfnode_to_adjoin_.end()) {
        // If input is not used in the network, just return zeros_like() as dout;
        MS_LOG(WARNING) << "Input is not used in network, input: " << input->ToString();
        auto dout = BuildZerosLikeNode(tape_, input);
        grad_inputs_list.push_back(dout);
      } else {
        grad_inputs_list.push_back(input_adjoint_iter->second->RealDout());
      }
      grad_inputs_abs_list.push_back(grad_inputs_list.back()->abstract());
    }
    grad_inputs_spec = std::make_shared<abstract::AbstractTuple>(grad_inputs_abs_list);
  }

  AnfNodePtrList grad_weights_list{NewValueNode(prim::kPrimMakeTuple)};
  AbstractBasePtr grad_weights_spec;
  if (grad_weights) {
    AbstractBasePtrList grad_weights_abs_list;
    for (const auto &weight : weights) {
      MS_EXCEPTION_IF_NULL(weight);
      auto input_adjoint_iter = anfnode_to_adjoin_.find(weight);
      if (input_adjoint_iter == anfnode_to_adjoin_.end()) {
        // If weight is not used in the network, just return zeros_like() as dout;
        MS_LOG(WARNING) << "Weight is not used in network, weight: " << weight->ToString();
        auto input_w = weight->cast<ParameterPtr>();
        MS_EXCEPTION_IF_NULL(input_w);
        auto default_param = input_w->default_param();
        MS_EXCEPTION_IF_NULL(default_param);
        auto dout = BuildZerosLikeValue(tape_, default_param);
        grad_weights_list.push_back(dout);
      } else {
        grad_weights_list.push_back(input_adjoint_iter->second->RealDout());
      }
      grad_weights_abs_list.push_back(grad_weights_list.back()->abstract());
    }
    grad_weights_spec = std::make_shared<abstract::AbstractTuple>(grad_weights_abs_list);
  }

  AnfNodePtr tape_output;
  if (grad_inputs && grad_weights) {
    tape_output = tape_->NewCNode(
      {NewValueNode(prim::kPrimMakeTuple), tape_->NewCNode(grad_inputs_list), tape_->NewCNode(grad_weights_list)});
    tape_output->set_abstract(std::make_shared<abstract::AbstractTuple>(abstract::AbstractBasePtrList{grad_inputs_spec, grad_weights_spec}));
  } else if (grad_inputs) {
    tape_output = tape_->NewCNode(grad_inputs_list);
    tape_output->set_abstract(grad_inputs_spec);
  } else if (grad_weights) {
    tape_output = tape_->NewCNode(grad_weights_list);
    tape_output->set_abstract(grad_weights_spec);
  } else if (cell_inputs_.empty()) {
    tape_output = tape_->NewCNode(grad_inputs_list);
    tape_output->set_abstract(grad_inputs_spec);
  } else {
    auto input_adjoint_iter = anfnode_to_adjoin_.find(cell_inputs_[0]);
    if (input_adjoint_iter == anfnode_to_adjoin_.end()) {
      // If input is not used in the network, just return zeros_like() as dout;
      MS_LOG(WARNING) << "Input is not used in network, input: " << cell_inputs_[0]->ToString();
      tape_output = BuildZerosLikeNode(tape_, cell_inputs_[0]);
    } else {
      tape_output = input_adjoint_iter->second->RealDout();
    }
  }
  tape_->set_output(tape_output);
}
}  // namespace ad
}  // namespace mindspore
