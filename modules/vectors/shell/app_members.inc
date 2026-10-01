// App members of module "vectors": a fragment spliced into class App.

void run_dot(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept;
void run_cross(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept;
void run_projection(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept;
void render_result_dot(const ResultState& s) noexcept;
void render_projection_result(const ProjectionResultState& s) noexcept;
void update_projection_result(ProjectionResultState& s) noexcept;
