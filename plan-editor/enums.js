window.enums = {
	plan_priority_action: [
		{e: 'dig', n: 'Dig (queue)', c: 'Room'},
		{e: 'dig_immediate', n: 'Dig (immediate)', c: 'Room'},
		{e: 'unignore_furniture', n: 'Build ignored furniture', c: 'Room'},
		{e: 'finish', n: 'Build furniture and smooth surfaces', c: 'Room'},
		{e: 'start_ore_search', n: 'Allow mining for metal', c: 'Global'},
		{e: 'past_initial_phase', n: 'Stop reclaiming unused furniture', c: 'Global'},
		{e: 'deconstruct_wagons', n: 'Disassemble wagon', c: 'Global'},
		{e: 'dig_next_cavern_outpost', n: 'Plan next cavern outpost', c: 'Global'}
	],
	room_status: [
		{e: 'plan', n: 'Planned'},
		{e: 'dig', n: 'Digging'},
		{e: 'dug', n: 'Constructing'},
		{e: 'finished', n: 'Finished'}
	],
	room_type: [
		{e: 'corridor', n: 'Corridor', ste: 'corridor_type'},
		{e: 'dininghall', n: 'Dining hall', c: 'Lifestyle'},
		{e: 'infirmary', n: 'Infirmary', c: 'Lifestyle'},
		{e: 'location', n: 'Location', c: 'Lifestyle', ste: 'location_type'},
		{e: 'bedroom', n: 'Bedroom', c: 'Lifestyle'},
		{e: 'nobleroom', n: 'Noble room', c: 'Lifestyle', ste: 'nobleroom_type'},
		{e: 'cemetery', n: 'Cemetery', c: 'Lifestyle'},
		{e: 'farmplot', n: 'Farm plot', c: 'Farming', ste: 'farm_type'},
		{e: 'pasture', n: 'Pasture', c: 'Farming'},
		{e: 'barracks', n: 'Barracks', c: 'Military'},
		{e: 'pitcage', n: 'Pitting area', c: 'Military'},
		{e: 'stockpile', n: 'Stockpile', c: 'Commerce', ste: 'stockpile_type'},
		{e: 'tradedepot', n: 'Trade depot', c: 'Commerce', fh: 5, fw: 5},
		{e: 'workshop', n: 'Workshop', c: 'Commerce', ste: 'workshop_type'},
		{e: 'furnace', n: 'Furnace', c: 'Commerce', ste: 'furnace_type'},
		{e: 'pond', n: 'Pond', c: 'Utility'},
		{e: 'cistern', n: 'Cistern', c: 'Utility', ste: 'cistern_type'},
		{e: 'garbagedump', n: 'Garbage dump', c: 'Utility'},
		{e: 'outpost', n: 'Outpost', c: 'Utility', ste: 'outpost_type', nc: true}
	],
	corridor_type: [
		{e: 'corridor', n: 'Corridor'},
		{e: 'veinshaft', n: 'Mining shaft', nc: true},
		{e: 'aqueduct', n: 'Aqueduct', nc: true},
		{e: 'outpost', n: 'Outpost', nc: true},
		{e: 'walkable', n: 'River tunnel', nc: true}
	],
	farm_type: [
		{e: 'food', n: 'Food'},
		{e: 'cloth', n: 'Cloth'}
	],
	stockpile_type: [
		{e: 'wood', n: 'Wood', c: 'Materials'},
		{e: 'stone', n: 'Stone', c: 'Materials'},
		{e: 'bars_blocks', n: 'Bars/Blocks', c: 'Materials'},
		{e: 'cloth', n: 'Cloth/Thread', c: 'Materials'},
		{e: 'leather', n: 'Leather', c: 'Materials'},
		{e: 'fresh_raw_hide', n: 'Untanned hides', c: 'Materials'},
		{e: 'gems', n: 'Gems', c: 'Materials'},
		{e: 'food', n: 'Food', c: 'Supplies', ss1: 'Prepared food'},
		{e: 'weapons', n: 'Weapons', c: 'Supplies', ss1: 'Usable', ss2: 'Unusable'},
		{e: 'armor', n: 'Armor/Clothing', c: 'Supplies', ss1: 'Usable', ss2: 'Unusable'},
		{e: 'ammo', n: 'Ammo', c: 'Supplies'},
		{e: 'sheets', n: 'Sheets', c: 'Supplies'},
		{e: 'coins', n: 'Coins', c: 'Supplies'},
		{e: 'furniture', n: 'Furniture', c: 'Supplies'},
		{e: 'finished_goods', n: 'Finished goods', c: 'Supplies'},
		{e: 'refuse', n: 'Refuse', c: 'Other'},
		{e: 'corpses', n: 'Corpses', c: 'Other'},
		{e: 'animals', n: 'Animals/Prisoners', c: 'Other', ss1: 'Empty cages', ss2: 'Empty animal traps'}
	],
	nobleroom_type: [
		{e: 'office', n: 'Office'},
		{e: 'bedroom', n: 'Bedroom'},
		{e: 'dining', n: 'Dining room'},
		{e: 'tomb', n: 'Tomb'}
	],
	outpost_type: [
		{e: 'cavern', n: 'Cavern'}
	],
	location_type: [
		{e: 'tavern', n: 'Tavern'},
		{e: 'library', n: 'Library'},
		{e: 'temple', n: 'Temple'}
	],
	cistern_type: [
		{e: 'well', n: 'Well'},
		{e: 'reserve', n: 'Reserve'}
	],
	layout_type: [
		{e: 'none', n: '(no furniture)'},
		{e: 'bed', n: 'Bed', c: 'Furniture'},
		{e: 'chair', n: 'Chair', c: 'Furniture'},
		{e: 'table', n: 'Table', c: 'Furniture'},
		{e: 'chest', n: 'Chest', c: 'Storage'},
		{e: 'cabinet', n: 'Cabinet', c: 'Storage'},
		{e: 'bookcase', n: 'Bookcase', c: 'Storage'},
		{e: 'archery_target', n: 'Archery Target', c: 'Military'},
		{e: 'armor_stand', n: 'Armor Stand', c: 'Military'},
		{e: 'weapon_rack', n: 'Weapon Rack', c: 'Military'},
		{e: 'hive', n: 'Hive', c: 'Farm'},
		{e: 'nest_box', n: 'Nest Box', c: 'Farm'},
		{e: 'coffin', n: 'Coffin', c: 'Utilities'},
		{e: 'traction_bench', n: 'Traction Bench', c: 'Utilities'},
		{e: 'well', n: 'Well', c: 'Utilities'},
		{e: 'door', n: 'Door', c: 'Gates'},
		{e: 'hatch', n: 'Hatch', c: 'Gates'},
		{e: 'floodgate', n: 'Floodgate', c: 'Gates'},
		{e: 'cage_trap', n: 'Cage Trap', c: 'Machines'},
		{e: 'lever', n: 'Lever', c: 'Machines'},
		{e: 'roller', n: 'Roller', c: 'Machines'},
		{e: 'track_stop', n: 'Track Stop', c: 'Machines'},
		{e: 'vertical_axle', n: 'Vertical Axle', c: 'Machines'},
		{e: 'gear_assembly', n: 'Gear Assembly', c: 'Machines'},
		{e: 'windmill', n: 'Windmill', c: 'Machines'}
	],
	workshop_type: [
		{e: 'Custom', n: 'Custom', raw: true},
		{e: 'Millstone', n: 'Millstone', c: '1\u00d71', fh: 1, fw: 1},
		{e: 'Quern', n: 'Quern', c: '1\u00d71', fh: 1, fw: 1},
		{e: 'Carpenters', n: 'Carpenters', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Masons', n: 'Masons', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Craftsdwarfs', n: 'Craftsdwarfs', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Mechanics', n: 'Mechanics', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Jewelers', n: 'Jewelers', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Bowyers', n: 'Bowyer\'s', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Farmers', n: 'Farmers', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Fishery', n: 'Fishery', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Butchers', n: 'Butcher\'s', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Kitchen', n: 'Kitchen', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Still', n: 'Still', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Loom', n: 'Loom', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Clothiers', n: 'Clothier\'s', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Dyers', n: 'Dyer\'s', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Tanners', n: 'Tanner\'s', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Leatherworks', n: 'Leatherworks', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Ashery', n: 'Ashery', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'MetalsmithsForge', n: 'Metalsmith\'s Forge', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'MagmaForge', n: 'Magma Forge', c: '3\u00d73', fh: 3, fw: 3},
		{e: 'Kennels', n: 'Kennels', c: '5\u00d75', fh: 5, fw: 5},
		{e: 'Siege', n: 'Siege', c: '5\u00d75', fh: 5, fw: 5}
		// Tool
	],
	furnace_type: [
		{e: 'Custom', n: 'Custom', raw: true},
		{e: 'WoodFurnace', n: 'WoodFurnace', fh: 3, fw: 3},
		{e: 'Smelter', n: 'Smelter', c: 'Fueled', fh: 3, fw: 3},
		{e: 'GlassFurnace', n: 'GlassFurnace', c: 'Fueled', fh: 3, fw: 3},
		{e: 'Kiln', n: 'Kiln', c: 'Fueled', fh: 3, fw: 3},
		{e: 'MagmaSmelter', n: 'MagmaSmelter', c: 'Magma', fh: 3, fw: 3},
		{e: 'MagmaGlassFurnace', n: 'MagmaGlassFurnace', c: 'Magma', fh: 3, fw: 3},
		{e: 'MagmaKiln', n: 'MagmaKiln', c: 'Magma', fh: 3, fw: 3}
	],
	stockpile_list: [
		// Animals
		// Food
		{e: 'FoodMeat', n: 'Meat', c: 'Food', st: 'food'},
		{e: 'FoodFish', n: 'Fish (prepared)', c: 'Food', st: 'food'},
		{e: 'FoodUnpreparedFish', n: 'Fish (raw)', c: 'Food', st: 'food'},
		{e: 'FoodEgg', n: 'Eggs', c: 'Food', st: 'food'},
		{e: 'FoodPlants', n: 'Plants', c: 'Food', st: 'food'},
		{e: 'FoodDrinkPlant', n: 'Drinks (from plants)', c: 'Food', st: 'food'},
		{e: 'FoodDrinkAnimal', n: 'Drinks (from animals)', c: 'Food', st: 'food'},
		{e: 'FoodCheesePlant', n: 'Cheese (from plants)', c: 'Food', st: 'food'},
		{e: 'FoodCheeseAnimal', n: 'Cheese (from animals)', c: 'Food', st: 'food'},
		{e: 'FoodSeeds', n: 'Seeds', c: 'Food', st: 'food'},
		{e: 'FoodLeaves', n: 'Leaves and Fruits', c: 'Food', st: 'food'},
		{e: 'FoodMilledPlant', n: 'Milled plants', c: 'Food', st: 'food'},
		{e: 'FoodBoneMeal', n: 'Bone meal', c: 'Food', st: 'food'},
		{e: 'FoodFat', n: 'Fat', c: 'Food', st: 'food'},
		{e: 'FoodPaste', n: 'Paste', c: 'Food', st: 'food'},
		{e: 'FoodPressedMaterial', n: 'Pressed material', c: 'Food', st: 'food'},
		{e: 'FoodExtractPlant', n: 'Extracts (from plants)', c: 'Food', st: 'food'},
		{e: 'FoodExtractAnimal', n: 'Extracts (from animals)', c: 'Food', st: 'food'},
		{e: 'FoodMiscLiquid', n: 'Misc. liquids', c: 'Food', st: 'food'},
		// Furniture
		// FurnitureType
		{e: 'FurnitureStoneClay', n: 'Stone or clay', c: 'Furniture', st: 'furniture'},
		{e: 'FurnitureMetal', n: 'Metal', c: 'Furniture', st: 'furniture'},
		{e: 'FurnitureOtherMaterials', n: 'Other materials', c: 'Furniture', st: 'furniture'},
		// FurnitureCoreQuality
		// FurnitureTotalQuality
		// Corpses
		// Refuse
		{e: 'RefuseItems', n: 'Rotting or worn-out items', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseCorpses', n: 'Corpses', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseParts', n: 'Severed body parts', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseSkulls', n: 'Skulls', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseBones', n: 'Bones', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseShells', n: 'Shells', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseTeeth', n: 'Teeth', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseHorns', n: 'Horns', c: 'Refuse', st: 'refuse'},
		{e: 'RefuseHair', n: 'Hair', c: 'Refuse', st: 'refuse'},
		// Stone
		{e: 'StoneOres', n: 'Ores', c: 'Stone', st: 'stone'},
		{e: 'StoneEconomic', n: 'Economic', c: 'Stone', st: 'stone'},
		{e: 'StoneClay', n: 'Clay', c: 'Stone', st: 'stone'},
		{e: 'StoneOther', n: 'Other', c: 'Stone', st: 'stone'},
		// Ammo
		// AmmoType
		{e: 'AmmoMetal', n: 'Metal', c: 'Ammo', st: 'ammo'},
		{e: 'AmmoOther', n: 'Other', c: 'Ammo', st: 'ammo'},
		// AmmoCoreQuality
		// AmmoTotalQuality
		// Coins
		// BarsBlocks
		{e: 'BarsMetal', n: 'Metal', c: 'Bars', st: 'bars_blocks'},
		{e: 'BarsOther', n: 'Other', c: 'Bars', st: 'bars_blocks'},
		{e: 'BlocksStone', n: 'Stone', c: 'Blocks', st: 'bars_blocks'},
		{e: 'BlocksMetal', n: 'Metal', c: 'Blocks', st: 'bars_blocks'},
		{e: 'BlocksOther', n: 'Other', c: 'Blocks', st: 'bars_blocks'},
		// Gems
		{e: 'RoughGem', n: 'Gems', c: 'Rough', st: 'gems'},
		{e: 'RoughGlass', n: 'Glass', c: 'Rough', st: 'gems'},
		{e: 'CutGem', n: 'Gems', c: 'Cut', st: 'gems'},
		{e: 'CutGlass', n: 'Glass', c: 'Cut', st: 'gems'},
		{e: 'CutStone', n: 'Stone', c: 'Cut', st: 'gems'},
		// Goods
		// GoodsType
		{e: 'GoodsStone', n: 'Stone', c: 'Finished goods', st: 'finished_goods'},
		{e: 'GoodsMetal', n: 'Metal', c: 'Finished goods', st: 'finished_goods'},
		{e: 'GoodsGem', n: 'Gem', c: 'Finished goods', st: 'finished_goods'},
		{e: 'GoodsOther', n: 'Other', c: 'Finished goods', st: 'finished_goods'},
		// GoodsCoreQuality
		// GoodsTotalQuality
		// Leather
		// Cloth
		{e: 'ThreadSilk', n: 'Silk', c: 'Thread', st: 'cloth'},
		{e: 'ThreadPlant', n: 'Plant', c: 'Thread', st: 'cloth'},
		{e: 'ThreadYarn', n: 'Yarn', c: 'Thread', st: 'cloth'},
		{e: 'ThreadMetal', n: 'Metal', c: 'Thread', st: 'cloth'},
		{e: 'ClothSilk', n: 'Silk', c: 'Cloth', st: 'cloth'},
		{e: 'ClothPlant', n: 'Plant', c: 'Cloth', st: 'cloth'},
		{e: 'ClothYarn', n: 'Yarn', c: 'Cloth', st: 'cloth'},
		{e: 'ClothMetal', n: 'Metal', c: 'Cloth', st: 'cloth'},
		// Wood
		// Weapons
		{e: 'WeaponsType', n: 'Weapons', c: 'Weapons', st: 'weapons'},
		{e: 'WeaponsTrapcomp', n: 'Trap components', c: 'Weapons', st: 'weapons'},
		{e: 'WeaponsMetal', n: 'Metal', c: 'Weapons', st: 'weapons'},
		{e: 'WeaponsStone', n: 'Stone', c: 'Weapons', st: 'weapons'},
		{e: 'WeaponsOther', n: 'Other', c: 'Weapons', st: 'weapons'},
		// WeaponsCoreQuality
		// WeaponsTotalQuality
		// Armor
		{e: 'ArmorMetal', n: 'Metal', c: 'Armor', st: 'armor'},
		{e: 'ArmorOther', n: 'Other (cloth, leather, stone, or wood)', c: 'Armor', st: 'armor'},
		{e: 'ArmorHead', n: 'Headwear', c: 'Armor', st: 'armor'},
		{e: 'ArmorBody', n: 'Bodywear', c: 'Armor', st: 'armor'},
		{e: 'ArmorHands', n: 'Handwear', c: 'Armor', st: 'armor'},
		{e: 'ArmorLegs', n: 'Legwear', c: 'Armor', st: 'armor'},
		{e: 'ArmorFeet', n: 'Footwear', c: 'Armor', st: 'armor'},
		{e: 'ArmorShield', n: 'Shields', c: 'Armor', st: 'armor'},
		// ArmorCoreQuality
		// ArmorTotalQuality
		// Sheet
		{e: 'SheetPaper', n: 'Paper (from plants)', c: 'Sheets', st: 'sheet'},
		{e: 'SheetParchment', n: 'Parchment (from animals)', c: 'Sheets', st: 'sheet'}
		// AdditionalOptions
	],
	construction_type: [
		{e: 'NONE', n: '(none)'},
		{e: 'Fortification', n: 'Fortification'},
		{e: 'Wall', n: 'Wall'},
		{e: 'Floor', n: 'Floor'},
		{e: 'UpStair', n: 'Up staircase'},
		{e: 'DownStair', n: 'Down staircase'},
		{e: 'UpDownStair', n: 'Up/Down staircase'},
		{e: 'Ramp', n: 'Ramp'},
		{e: 'TrackN', n: 'Track (N)'},
		{e: 'TrackS', n: 'Track (S)'},
		{e: 'TrackE', n: 'Track (E)'},
		{e: 'TrackW', n: 'Track (W)'},
		{e: 'TrackNS', n: 'Track (NS)'},
		{e: 'TrackNE', n: 'Track (NE)'},
		{e: 'TrackNW', n: 'Track (NW)'},
		{e: 'TrackSE', n: 'Track (SE)'},
		{e: 'TrackSW', n: 'Track (SW)'},
		{e: 'TrackEW', n: 'Track (EW)'},
		{e: 'TrackNSE', n: 'Track (NSE)'},
		{e: 'TrackNSW', n: 'Track (NSW)'},
		{e: 'TrackNEW', n: 'Track (NEW)'},
		{e: 'TrackSEW', n: 'Track (SEW)'},
		{e: 'TrackNSEW', n: 'Track (NSEW)'},
		{e: 'TrackRampN', n: 'Track ramp (N)'},
		{e: 'TrackRampS', n: 'Track ramp (S)'},
		{e: 'TrackRampE', n: 'Track ramp (E)'},
		{e: 'TrackRampW', n: 'Track ramp (W)'},
		{e: 'TrackRampNS', n: 'Track ramp (NS)'},
		{e: 'TrackRampNE', n: 'Track ramp (NE)'},
		{e: 'TrackRampNW', n: 'Track ramp (NW)'},
		{e: 'TrackRampSE', n: 'Track ramp (SE)'},
		{e: 'TrackRampSW', n: 'Track ramp (SW)'},
		{e: 'TrackRampEW', n: 'Track ramp (EW)'},
		{e: 'TrackRampNSE', n: 'Track ramp (NSE)'},
		{e: 'TrackRampNSW', n: 'Track ramp (NSW)'},
		{e: 'TrackRampNEW', n: 'Track ramp (NEW)'},
		{e: 'TrackRampSEW', n: 'Track ramp (SEW)'},
		{e: 'TrackRampNSEW', n: 'Track ramp (NSEW)'}
	],
	tile_dig_designation: [
		{e: 'No', n: 'No (leave as a wall)'},
		{e: 'Default', n: 'Default (floor)'},
		{e: 'Channel', n: 'Channel (make a hole in the floor)'},
		{e: 'Ramp', n: 'Ramp (make a hole in the ceiling)'},
		{e: 'UpStair', n: 'Up staircase'},
		{e: 'DownStair', n: 'Down staircase'},
		{e: 'UpDownStair', n: 'Up/Down staircase'}
	]
};

