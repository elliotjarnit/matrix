// App members of module "cramer": a fragment spliced into class App.

void run_cramer(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept;
bool open_cramer_steps(const ResultState& s) noexcept;
void render_cramer_steps_menu(const CramerStepsMenuState& s) noexcept;
void update_cramer_steps_menu(CramerStepsMenuState& s) noexcept;
