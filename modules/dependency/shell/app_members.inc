// App members of module "dependency": a fragment spliced into class App.

void run_span_test(OperationId op, std::uint8_t slot, std::uint8_t unused) noexcept;
void run_indep_test(OperationId op, std::uint8_t slot, std::uint8_t unused) noexcept;
void run_transformation(OperationId op, std::uint8_t slot, std::uint8_t unused) noexcept;
void render_result_span_indep(const ResultState& s) noexcept;
void render_result_transformation(const ResultState& s) noexcept;
