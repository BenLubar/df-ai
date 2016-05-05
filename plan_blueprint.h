const static int16_t MinX, MinY, MinZ;
const static int16_t MaxX, MaxY, MaxZ;

command_result setup_blueprint(color_ostream & out);

command_result scan_fort_entrance(color_ostream & out);
command_result scan_fort_body(color_ostream & out);
command_result setup_blueprint_rooms(color_ostream & out);
command_result setup_blueprint_workshops(color_ostream & out, df::coord f, const std::vector<room *> & entr);
command_result setup_blueprint_stockpiles(color_ostream & out, df::coord f, const std::vector<room *> & entr);
command_result setup_blueprint_pitcage(color_ostream & out);
command_result setup_blueprint_utilities(color_ostream & out, df::coord f, const std::vector<room *> & entr);
command_result setup_blueprint_cistern_fromsource(color_ostream & out, df::coord src, df::coord f, room *tavern);
command_result setup_blueprint_pastures(color_ostream & out);
command_result setup_blueprint_outdoor_farms(color_ostream & out, size_t want);
command_result setup_blueprint_bedrooms(color_ostream & out, df::coord f, const std::vector<room *> & entr, int level);
command_result setup_outdoor_gathering_zones(color_ostream & out);
command_result setup_blueprint_caverns(color_ostream & out);
