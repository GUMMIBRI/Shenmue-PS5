// SPDX-License-Identifier: GPL-3.0-only
// Source-informed by new-yokosuka NativeDialogueSelector.js/NativeDialoguePredicate.js
// b0df7f2720a7eecabce2e610fdedec35bf1243e3; see docs/DIALOGUE-EXPRESSION.md.
#include "cedar/dialogue_expression.hpp"
#include <cmath>
#include <limits>

namespace cedar {
namespace {
using E = DialogueExpressionError;
struct Span { std::uintptr_t begin{}, end{}; };
bool span(const void* p, std::size_t count, std::size_t item, Span& result) noexcept {
    const auto maximum = std::numeric_limits<std::uintptr_t>::max();
    result.begin = reinterpret_cast<std::uintptr_t>(p);
    if (count > maximum / item || count*item > maximum - result.begin) return false;
    result.end = result.begin + count*item;
    return true;
}
bool overlap(Span a, Span b) noexcept {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
bool aligned(const void* p, std::size_t alignment) noexcept {
    return reinterpret_cast<std::uintptr_t>(p) % alignment == 0;
}
DialogueExpressionEvaluation eval_error(E error, std::size_t index = DialogueExpressionResult::no_offset) noexcept {
    return {error, false, 0, index};
}
} // namespace

DialogueExpressionResult parse_dialogue_expression(const std::uint8_t* bytes, std::size_t size,
    std::size_t start, DialogueExpressionNode* nodes, std::size_t node_capacity,
    std::size_t* stack, std::size_t stack_capacity, DialogueExpressionView& output) noexcept {
    if (!bytes || !nodes || !stack || !aligned(nodes, alignof(DialogueExpressionNode)) ||
        !aligned(stack, alignof(std::size_t))) return {E::invalid_buffer, start};
    Span input{}, ns{}, ss{}, out{};
    if (!span(bytes,size,1,input) || !span(nodes,node_capacity,sizeof(*nodes),ns) ||
        !span(stack,stack_capacity,sizeof(*stack),ss) || !span(&output,1,sizeof(output),out))
        return {E::address_overflow,start};
    if (overlap(input,ns) || overlap(input,ss) || overlap(input,out) ||
        overlap(ns,ss) || overlap(ns,out) || overlap(ss,out)) return {E::overlapping_storage,start};
    if (start >= size) return {E::offset_out_of_range,start};
    std::size_t cursor = start, count = 0, top = 0, tokens = 0;
    while (cursor < size) {
        const auto at = cursor;
        if (++tokens > DialogueExpressionView::max_tokens) return {E::resource_limit,at};
        const auto token = bytes[cursor++];
        const auto group = token & 0xe0;
        DialogueExpressionNode value{};
        value.subtree_begin = count; value.depth = 1;
        if (group != 0x80) {
            if (cursor == size) return {E::truncated_operand,at};
            const auto next = bytes[cursor++];
            value.operand.index = static_cast<std::uint16_t>(((token & 15) << 8) | next);
            switch (group) {
            case 0x00: case 0x20: case 0x40:
                value.operand.kind = DialogueOperandKind::state_bank;
                value.operand.native_value_type = static_cast<std::uint8_t>(2 + group/0x20);
                if (group == 0x40 && value.operand.index == 0xfff) {
                    value.operand.kind = DialogueOperandKind::literal;
                    value.operand.native_value_type = 1; value.literal = -1;
                }
                break;
            case 0x60:
                value.operand.kind = DialogueOperandKind::literal;
                value.operand.native_value_type = 1; value.literal = value.operand.index; break;
            case 0xa0:
                if (token != 0xa0) return {E::unsupported_token,at};
                value.operand.kind = DialogueOperandKind::runtime_selector;
                value.operand.native_value_type = 1; value.operand.index = next; break;
            case 0xc0:
                value.operand.kind = DialogueOperandKind::actor_identity;
                value.operand.native_value_type = 5; break;
            case 0xe0:
                value.operand.kind = DialogueOperandKind::spatial_result;
                value.operand.native_value_type = 6; break;
            default: return {E::unsupported_token,at};
            }
            if (top == stack_capacity) return {E::stack_capacity,at};
        } else {
            value.operation = token & 31;
            if (!value.operation) {
                if (!top) return {E::empty_expression,at};
                DialogueExpressionView view;
                view.nodes_ = nodes; view.count_ = count; view.root_ = stack[top-1];
                view.roots_ = top; view.end_ = cursor;
                output = view;
                return {};
            }
            if (value.operation > 12) return {E::unsupported_operator,at};
            const bool unary = value.operation >= 2 && value.operation <= 4;
            if (top < (unary ? 1u : 2u)) return {E::stack_underflow,at};
            if (unary) {
                value.left = stack[top-1];
                value.subtree_begin = nodes[value.left].subtree_begin;
                value.depth = nodes[value.left].depth + 1;
            } else {
                value.left = stack[top-2]; value.right = stack[top-1];
                value.subtree_begin = nodes[value.left].subtree_begin;
                value.depth = (nodes[value.left].depth > nodes[value.right].depth ?
                    nodes[value.left].depth : nodes[value.right].depth) + 1;
            }
            if (value.depth > DialogueExpressionView::max_depth) return {E::resource_limit,at};
            top -= unary ? 1 : 2;
        }
        if (count == DialogueExpressionView::max_nodes) return {E::resource_limit,at};
        if (count == node_capacity) return {E::node_capacity,at};
        nodes[count] = value;
        stack[top++] = count++;
    }
    return {E::unterminated,cursor};
}

DialogueExpressionEvaluation evaluate_dialogue_expression(const DialogueExpressionView& view,
    DialogueExpressionProvider provider, void* context, DialogueExpressionValue* values,
    std::size_t value_capacity) noexcept {
    const auto count = view.node_count();
    if (!view.parsed() || !count || count > DialogueExpressionView::max_nodes ||
        view.root_index() >= count) return eval_error(E::invalid_view);
    if (!values || !aligned(values,alignof(DialogueExpressionValue))) return eval_error(E::invalid_buffer);
    Span ns{}, vs{}, object{};
    if (!span(view.node(0),count,sizeof(DialogueExpressionNode),ns) ||
        !span(values,value_capacity,sizeof(*values),vs) || !span(&view,1,sizeof(view),object))
        return eval_error(E::address_overflow);
    if (overlap(ns,vs) || overlap(object,vs)) return eval_error(E::overlapping_storage);
    if (value_capacity < count) return eval_error(E::value_capacity);
    const auto root = view.root_index();
    const auto begin = view.node(root)->subtree_begin;
    if (begin > root) return eval_error(E::invalid_view);
    // Preflight the complete selected subtree before invoking a host provider.
    for (std::size_t i = begin; i <= root; ++i) {
        const auto& n = *view.node(i);
        if (n.subtree_begin < begin || n.subtree_begin > i || !n.depth ||
            n.depth > DialogueExpressionView::max_depth) return eval_error(E::invalid_view,i);
        if (!n.operation) {
            if (n.subtree_begin != i || n.depth != 1) return eval_error(E::invalid_view,i);
            switch (n.operand.kind) {
            case DialogueOperandKind::literal:
                if (!std::isfinite(n.literal)) return eval_error(E::non_finite,i);
                break;
            case DialogueOperandKind::state_bank:
                if (n.operand.native_value_type < 2 || n.operand.native_value_type > 4 || n.operand.index > 4095)
                    return eval_error(E::invalid_view,i);
                break;
            case DialogueOperandKind::runtime_selector:
                if (n.operand.index > 255) return eval_error(E::invalid_view,i);
                break;
            case DialogueOperandKind::actor_identity: case DialogueOperandKind::spatial_result:
                if (n.operand.index > 4095) return eval_error(E::invalid_view,i);
                break;
            default: return eval_error(E::invalid_view,i);
            }
        } else {
            if (n.operation > 12 || n.left < begin || n.left >= i) return eval_error(E::invalid_view,i);
            if (n.operation <= 3) return eval_error(E::unsupported_operation,i);
            if (n.operation != 4 && (n.right < begin || n.right >= i)) return eval_error(E::invalid_view,i);
        }
    }
    for (std::size_t i = begin; i <= root; ++i) {
        const auto& n = *view.node(i);
        DialogueExpressionValue result{};
        if (!n.operation) {
            if (n.operand.kind == DialogueOperandKind::literal) result = {true,n.literal};
            else if (provider) result = provider(context,n.operand);
            if (result.resolved && !std::isfinite(result.value)) return eval_error(E::non_finite,i);
            if (!result.resolved) result.value = 0; // Ignored, never a resolved default.
        } else {
            const auto left = values[n.left];
            const auto right = n.operation == 4 ? DialogueExpressionValue{} : values[n.right];
            if (n.operation == 4) {
                if (left.resolved) result = {true,left.value == 0 ? 1.0 : 0.0};
            } else if (n.operation == 5) {
                if ((left.resolved && left.value == 0) || (right.resolved && right.value == 0)) result = {true,0};
                else if (left.resolved && right.resolved) result = {true,1};
            } else if (n.operation == 6) {
                if ((left.resolved && left.value != 0) || (right.resolved && right.value != 0)) result = {true,1};
                else if (left.resolved && right.resolved) result = {true,0};
            } else if (left.resolved && right.resolved) {
                bool match = false;
                switch (n.operation) {
                case 7: match = left.value == right.value; break;
                case 8: match = left.value != right.value; break;
                case 9: match = left.value > right.value; break;
                case 10: match = left.value >= right.value; break;
                case 11: match = left.value < right.value; break;
                case 12: match = left.value <= right.value; break;
                }
                result = {true,match ? 1.0 : 0.0};
            }
        }
        values[i] = result;
    }
    const auto result = values[root];
    return {E::none,result.resolved,result.value,DialogueExpressionResult::no_offset};
}
} // namespace cedar
