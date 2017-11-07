(function() {
	window.openRoomEditor = function openRoomInstanceEditor(mainPanel, name, inst, tmpl) {
		function markInstanceDirty() {
			dirty = true;
			lastModified['df-ai-blueprints/rooms/instances/' + name + '/' + inst + '.json'] = new Date();
		}
		function markTemplateDirty() {
			dirty = true;
			lastModified['df-ai-blueprints/rooms/templates/' + name + '/' + tmpl + '.json'] = new Date();
		}

		mainPanel.innerHTML = '<h1>Unfortunately, the room editor is not yet available.</h>';

		var room = rooms[name];
		var instance = room.instances[inst];
		var template = room.templates[tmpl];
		debugger;
	};
})();

/*
bool room_base::furniture_t::apply(Json::Value data, std::string & error, bool allow_placeholders)
{
	std::ostringstream scratch;

	if (allow_placeholders && data.isMember("placeholder") && !apply_index(has_placeholder, placeholder, data, "placeholder", error))
	{
		return false;
	}

	if (data.isMember("type") && !apply_enum(type, data, "type", error))
	{
		return false;
	}

	if (data.isMember("construction") && !apply_enum(construction, data, "construction", error))
	{
		return false;
	}

	if (data.isMember("dig") && !apply_enum(dig, data, "dig", error))
	{
		return false;
	}

	if (data.isMember("x") && !apply_int(pos.x, data, "x", error))
	{
		return false;
	}
	if (data.isMember("y") && !apply_int(pos.y, data, "y", error))
	{
		return false;
	}
	if (data.isMember("z") && !apply_int(pos.z, data, "z", error))
	{
		return false;
	}

	if (data.isMember("target") && !apply_index(has_target, target, data, "target", error))
	{
		return false;
	}

	if (data.isMember("has_users") && !apply_int(has_users, data, "has_users", error))
	{
		return false;
	}

	if (data.isMember("ignore") && !apply_bool(ignore, data, "ignore", error))
	{
		return false;
	}

	if (data.isMember("makeroom") && !apply_bool(makeroom, data, "makeroom", error))
	{
		return false;
	}

	if (data.isMember("internal") && !apply_bool(internal, data, "internal", error))
	{
		return false;
	}

	if (data.isMember("comment") && !apply_variable_string(comment, data, "comment", error, true))
	{
		return false;
	}

	std::vector<std::string> remaining_members(data.getMemberNames());
	if (remaining_members.empty())
	{
		return true;
	}

	error = "";
	const char *before = "unhandled furniture properties: ";
	for (auto & m : remaining_members)
	{
		error += before;
		error += m;
		before = ", ";
	}

	return false;
}

bool room_base::room_t::apply(Json::Value data, std::string & error, bool allow_placeholders)
{
	if (allow_placeholders && data.isMember("placeholder") && !apply_index(has_placeholder, placeholder, data, "placeholder", error))
	{
		return false;
	}

	if (data.isMember("type") && !apply_enum(type, data, "type", error))
	{
		return false;
	}

	if (data.isMember("corridor_type") && !apply_enum(corridor_type, data, "corridor_type", error))
	{
		return false;
	}
	if (data.isMember("farm_type") && !apply_enum(farm_type, data, "farm_type", error))
	{
		return false;
	}
	if (data.isMember("stockpile_type") && !apply_enum(stockpile_type, data, "stockpile_type", error))
	{
		return false;
	}
	if (data.isMember("nobleroom_type") && !apply_enum(nobleroom_type, data, "nobleroom_type", error))
	{
		return false;
	}
	if (data.isMember("outpost_type") && !apply_enum(outpost_type, data, "outpost_type", error))
	{
		return false;
	}
	if (data.isMember("location_type") && !apply_enum(location_type, data, "location_type", error))
	{
		return false;
	}
	if (data.isMember("cistern_type") && !apply_enum(cistern_type, data, "cistern_type", error))
	{
		return false;
	}
	if (data.isMember("workshop_type") && !apply_enum(workshop_type, data, "workshop_type", error))
	{
		return false;
	}
	if (data.isMember("furnace_type") && !apply_enum(furnace_type, data, "furnace_type", error))
	{
		return false;
	}

	if (data.isMember("raw_type") && !apply_variable_string(raw_type, data, "raw_type", error))
	{
		return false;
	}

	if (data.isMember("comment") && !apply_variable_string(comment, data, "comment", error, true))
	{
		return false;
	}

	if (!min.isValid())
	{
		if (data.isMember("min"))
		{
			if (!apply_coord(min, data, "min", error))
			{
				return false;
			}
		}
		else
		{
			if (data.isMember("max"))
			{
				error = "missing min on room";
			}
			else
			{
				error = "missing min and max on room";
			}
			return false;
		}

		if (data.isMember("max"))
		{
			if (!apply_coord(max, data, "max", error))
			{
				return false;
			}
		}
		else
		{
			error = "missing max on room";
			return false;
		}

		if (min.x > max.x)
		{
			error = "min.x > max.x";
			return false;
		}
		if (min.y > max.y)
		{
			error = "min.y > max.y";
			return false;
		}
		if (min.z > max.z)
		{
			error = "min.z > max.z";
			return false;
		}

		if (data.isMember("exits"))
		{
			Json::Value value = data.removeMember("exits");
			if (!value.isArray())
			{
				error = "exits has wrong type (should be array)";
				return false;
			}

			for (auto exit : value)
			{
				if (!exit.isArray() || exit.size() < 4 || exit.size() > 5 || !exit[0].isString() || !exit[1].isInt() || !exit[2].isInt() || !exit[3].isInt() || (exit.size() > 4 && !exit[4].isObject()))
				{
					error = "exit has wrong type (should be [string, integer, integer, integer])";
					return false;
				}

				df::coord t(exit[1].asInt(), exit[2].asInt(), exit[3].asInt());
				std::map<std::string, variable_string> context;
				if (exit.size() > 4)
				{
					std::vector<std::string> vars(exit[4].getMemberNames());
					for (auto var : vars)
					{
						if (!apply_variable_string(context[var], exit[4], var, error))
						{
							error = "exit variable " + error;
							return false;
						}
					}
				}
				exits[t][exit[0].asString()] = context;
			}
		}
	}

	if (data.isMember("accesspath") && !apply_indexes(accesspath, data, "accesspath", error))
	{
		return false;
	}
	if (data.isMember("layout") && !apply_indexes(layout, data, "layout", error))
	{
		return false;
	}

	if (data.isMember("level") && !apply_int(level, data, "level", error))
	{
		return false;
	}
	if (data.isMember("noblesuite"))
	{
		bool is_noblesuite;
		if (!apply_index(is_noblesuite, noblesuite, data, "noblesuite", error))
		{
			return false;
		}
		if (!is_noblesuite)
		{
			noblesuite = -1;
		}
	}
	if (data.isMember("queue") && !apply_int(queue, data, "queue", error))
	{
		return false;
	}

	if (data.isMember("workshop") && !apply_index(has_workshop, workshop, data, "workshop", error))
	{
		return false;
	}

	if (data.isMember("stock_disable") && !apply_enum_set(stock_disable, data, "stock_disable", error))
	{
		return false;
	}
	if (data.isMember("stock_specific1") && !apply_bool(stock_specific1, data, "stock_specific1", error))
	{
		return false;
	}
	if (data.isMember("stock_specific2") && !apply_bool(stock_specific2, data, "stock_specific2", error))
	{
		return false;
	}

	if (data.isMember("has_users") && !apply_int(has_users, data, "has_users", error))
	{
		return false;
	}
	if (data.isMember("temporary") && !apply_bool(temporary, data, "temporary", error))
	{
		return false;
	}
	if (data.isMember("outdoor") && !apply_bool(outdoor, data, "outdoor", error))
	{
		return false;
	}
	if (data.isMember("single_biome") && !apply_bool(single_biome, data, "single_biome", error))
	{
		return false;
	}

	if (data.isMember("require_walls") && !apply_bool(require_walls, data, "require_walls", error))
	{
		return false;
	}
	if (data.isMember("require_floor") && !apply_bool(require_floor, data, "require_floor", error))
	{
		return false;
	}
	if (data.isMember("require_grass") && !apply_int(require_grass, data, "require_grass", error))
	{
		return false;
	}
	if (data.isMember("in_corridor") && !apply_bool(in_corridor, data, "in_corridor", error))
	{
		return false;
	}

	std::vector<std::string> remaining_members(data.getMemberNames());
	if (remaining_members.empty())
	{
		return true;
	}

	error = "";
	const char *before = "unhandled room properties: ";
	for (auto & m : remaining_members)
	{
		error += before;
		error += m;
		before = ", ";
	}

	return false;
}

bool room_base::apply(Json::Value data, std::string & error, bool allow_placeholders)
{
	if (data.isMember("f"))
	{
		if (!data["f"].isArray())
		{
			error = "f (furniture list) must be an array";
			return false;
		}

		for (auto & fdata : data["f"])
		{
			furniture_t *f = new furniture_t();
			if (!f->apply(fdata, error, allow_placeholders))
			{
				delete f;
				return false;
			}

			layout.push_back(f);
		}

		data.removeMember("f");
	}

	if (data.isMember("r"))
	{
		if (!data["r"].isArray())
		{
			error = "r (room list) must be an array";
			return false;
		}

		for (auto & rdata : data["r"])
		{
			room_t *r = new room_t();
			if (!r->apply(rdata, error, allow_placeholders))
			{
				delete r;
				return false;
			}

			rooms.push_back(r);
		}

		data.removeMember("r");
	}

	if (!check_indexes(layout, rooms, error))
	{
		return false;
	}

	std::vector<std::string> remaining_members(data.getMemberNames());
	if (remaining_members.empty())
	{
		return true;
	}

	error = "";
	const char *before = "unhandled properties: ";
	for (auto & m : remaining_members)
	{
		error += before;
		error += m;
		before = ", ";
	}

	return false;
}

bool room_template::apply(Json::Value data, std::string & error)
{
	min_placeholders = 0;

	if (!room_base::apply(data, error, true))
	{
		return false;
	}

	for (auto & f : layout)
	{
		if (f->has_placeholder)
		{
			min_placeholders = std::max(min_placeholders, f->placeholder + 1);
		}
	}
	for (auto & r : rooms)
	{
		if (r->has_placeholder)
		{
			min_placeholders = std::max(min_placeholders, r->placeholder + 1);
		}
	}

	return true;
}

bool room_instance::apply(Json::Value data, std::string & error)
{
	if (data.isMember("p"))
	{
		if (!data["p"].isArray())
		{
			error = "p (placeholder list) must be an array";
			return false;
		}

		for (auto & p : data["p"])
		{
			if (!p.isObject())
			{
				error = "placeholders must be objects";
				return false;
			}

			placeholders.push_back(new Json::Value(p));
		}

		data.removeMember("p");
	}

	return room_base::apply(data, error, false);
}
*/
