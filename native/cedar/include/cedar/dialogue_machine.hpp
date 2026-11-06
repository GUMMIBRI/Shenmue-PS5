// SPDX-License-Identifier: GPL-3.0-only
// Authored New Yokosuka behavior adaptation; see docs/DIALOGUE-MACHINE.md.
#pragma once
#include "cedar/dialogue_body.hpp"
#include "cedar/dialogue_expression.hpp"
namespace cedar {
enum class DialogueMachineStatus : std::uint8_t {
    ready, pending_host, complete, budget_exhausted, invalid_argument,
    invalid_state, decode_failure, predicate_required, host_failed, expression_failure
};
struct DialogueMachineState {
    std::size_t cursor{}, continuation60{}, continuation64{};
    bool has_continuation60{}, has_continuation64{};
    DialogueBodyMode mode{DialogueBodyMode::outer_body};
    bool predicate_valid{}, predicate_zero{};
    std::uint8_t message_flags{};
};
struct DialogueMachinePending {
    std::uint64_t token{}, identifier{};
    DialogueBodyInstruction instruction{};
    std::uint8_t message_flags{};
    bool requires_offset{};
};
struct DialogueMachineAck {
    std::uint64_t token{};
    bool succeeded{};
    bool has_offset{};
    std::size_t offset{};
};
// Route is borrowed immutable storage through reset/destruction; caller owns host
// effects and synchronization. No native opcode/expression evaluator is hidden.
class DialogueMachine {
public:
    DialogueMachine() noexcept = default;
    DialogueMachine(const DialogueMachine&) = delete;
    DialogueMachine& operator=(const DialogueMachine&) = delete;
    DialogueMachine(DialogueMachine&&) = delete;
    DialogueMachine& operator=(DialogueMachine&&) = delete;
    DialogueMachineStatus start(const std::uint8_t*,std::size_t,
        std::uint64_t identifier,const DialogueMachineState&) noexcept;
    DialogueMachineStatus advance(std::size_t budget) noexcept;
    DialogueMachineStatus acknowledge(const DialogueMachineAck&) noexcept;
    DialogueMachineStatus supply_predicate(bool zero) noexcept;
    // Borrowed read-only provider/context; never reenter or mutate this machine,
    // source route, expression workspace or provider data during evaluation.
    DialogueMachineStatus bind_expression_provider(DialogueExpressionProvider, void*) noexcept;
    DialogueExpressionResult expression_parse_result() const noexcept { return expression_parse_; }
    DialogueExpressionEvaluation expression_evaluation() const noexcept { return expression_eval_; }
    const DialogueMachinePending* pending() const noexcept { return pending_?&request_:nullptr; }
    DialogueMachineState state() const noexcept { return state_; }
    std::size_t route_size() const noexcept { return size_; }
    DialogueExpressionProvider expression_provider() const noexcept { return provider_; }
    void* expression_provider_context() const noexcept { return provider_context_; }
    DialogueBodyError decode_error() const noexcept { return decode_error_; }
    void reset() noexcept;
private:
    const std::uint8_t* bytes_{};
    std::size_t size_{};
    std::uint64_t identifier_{}, serial_{};
    DialogueMachineState state_{};
    DialogueMachinePending request_{};
    DialogueBodyError decode_error_{DialogueBodyError::none};
    bool pending_{}, done_{};
    static constexpr std::size_t expression_capacity=256;
    DialogueExpressionNode expression_nodes_[expression_capacity]{};
    std::size_t expression_stack_[expression_capacity]{};
    DialogueExpressionValue expression_values_[expression_capacity]{};
    DialogueExpressionProvider provider_{};
    void* provider_context_{};
    DialogueExpressionResult expression_parse_{};
    DialogueExpressionEvaluation expression_eval_{};
    bool busy_{};
};
}
