// App members of module "area": a fragment spliced into class App.

void run_area(OperationId op, std::uint8_t slot, std::uint8_t unused) noexcept;
void run_area_transform(OperationId op, std::uint8_t a_slot, std::uint8_t p_slot) noexcept;
bool area_failed(const matrix_core::Error& err, std::uint8_t slot, matrix_core::MatrixView p, const char* dims_prefix) noexcept;
void push_area_result(
        OperationId op, std::uint8_t slot_a, std::uint8_t slot_b, matrix_core::MatrixView m, const matrix_core::Rational& area) noexcept;
void render_result_area(const ResultState& s) noexcept;
void render_result_area_transform(const ResultState& s) noexcept;
