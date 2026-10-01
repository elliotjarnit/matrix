// App members of module "lu": a fragment spliced into class App.

void run_lu_factor_op(OperationId op, std::uint8_t slot, std::uint8_t unused) noexcept;
void run_lu_solve_op(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept;
void render_result_lu_solve(const ResultState& s) noexcept;

// Shared by the slot picker and the row swap prompt. On success each pops the
// current page and pushes the result; when a swap is needed and `allow_swaps`
// is false they push the LuPivotConfirm page instead.
void run_lu_factor(std::uint8_t slot, bool allow_swaps) noexcept;
void run_lu_solve(std::uint8_t slot_a, std::uint8_t slot_b, bool allow_swaps) noexcept;

void render_lu_result(const LuResultState& s) noexcept;
void update_lu_result(LuResultState& s) noexcept;
void render_lu_confirm(const LuPivotConfirmState& s) noexcept;
void update_lu_confirm(LuPivotConfirmState& s) noexcept;
