(function() {
	window.addEventListener('beforeunload', function(e) {
		if (!dirty) {
			return;
		}

		var text = 'You have not saved your changes. Do you still want to leave?';
		e.returnValue = text;
		return text;
	}, false);

	var mainPanel = document.createElement('div');
	mainPanel.classList.add('main');
	document.body.appendChild(mainPanel);

	var sidePanel = document.createElement('div');
	sidePanel.classList.add('side');
	document.body.appendChild(sidePanel);

	var lastTags = [];

	var roomAndTagDataList = document.createElement('datalist');
	roomAndTagDataList.id = 'rooms-and-tags-list';
	document.body.appendChild(roomAndTagDataList);

	var roomDataList = document.createElement('datalist');
	roomDataList.id = 'rooms-list';
	document.body.appendChild(roomDataList);

	var instanceDataLists = document.createElement('div');
	instanceDataLists.style.display = 'none';
	document.body.appendChild(instanceDataLists);

	var saveLoad = document.createElement('p');
	saveLoad.id = 'save-load';
	sidePanel.appendChild(saveLoad);

	var loadButton = document.createElement('button');
	loadButton.textContent = 'Load';
	loadButton.addEventListener('click', doLoad, false);
	saveLoad.appendChild(loadButton);

	saveLoad.appendChild(document.createTextNode(' '));

	var saveButton = document.createElement('button');
	saveButton.textContent = 'Save';
	saveButton.addEventListener('click', doSave, false);
	saveLoad.appendChild(saveButton);

	var downloadBlurb = document.createElement('p');
	downloadBlurb.id = 'download-blurb';
	sidePanel.appendChild(downloadBlurb);

	downloadBlurb.appendChild(document.createTextNode('Don\'t want to start from scratch? Try using the '));

	var downloadLink = document.createElement('a');
	downloadLink.href = 'https://codeload.github.com/BenLubar/df-ai/zip/develop';
	downloadLink.textContent = 'latest development version of df-ai\'s built-in plans';
	downloadLink.addEventListener('click', function readZipDirectly(e) {
		e.preventDefault();
		if (dirty && !confirm('You have not saved your changes. Do you still want to load a different set of blueprints?')) {
			return;
		}

		gtag('event', 'dfai_load_existing', {
			url: downloadLink.href
		});
		document.getElementById('loading').style.display = 'block';
		lastModified = {};
		plans = {};
		rooms = {};
		doReadZip(new zip.HttpReader('https://cors-anywhere.herokuapp.com/' + downloadLink.href), function(err) {
			if (err) {
				downloadLink.removeEventListener('click', readZipDirectly);
			}
		});
	}, false);
	downloadBlurb.appendChild(downloadLink);

	downloadBlurb.appendChild(document.createTextNode(' as a starting point!'));

	window.promptName = function promptName(type, obj) {
		var name = prompt('What should the new ' + type + ' be named?', '');
		while (name !== null && name.length && (/[\/\\.]/.test(name) || Object.prototype.hasOwnProperty.call(obj, name))) {
			if (/[\/\\.]/.test(name)) {
				name = prompt('The name of a ' + type + ' cannot contain the following characters: / \\ .', name);
			} else {
				name = prompt('A ' + type + ' with that name already exists!', name);
			}
		}
		return name === null || !name.length ? null : name;
	};

	var plansHeading = document.createElement('h2');
	plansHeading.id = 'plans-heading';
	plansHeading.textContent = 'Plans';
	sidePanel.appendChild(plansHeading);

	var addPlanButton = document.createElement('button');
	addPlanButton.classList.add('add');
	addPlanButton.textContent = '+';
	addPlanButton.addEventListener('click', function() {
		var name = promptName('plan', plans);
		if (name === null) {
			return;
		}

		dirty = true;
		lastModified['df-ai-blueprints/plans/' + name + '.json'] = new Date();

		gtag('event', 'dfai_create_plan', {
			plan_name: name
		});

		plans[name] = {};
		addPlan(name);
		findPlan(name).querySelector('a').click();
	}, false);
	plansHeading.appendChild(addPlanButton);

	var planList = document.createElement('ul');
	planList.id = 'plans';
	sidePanel.appendChild(planList);

	var roomsHeading = document.createElement('h2');
	roomsHeading.id = 'rooms-heading';
	roomsHeading.textContent = 'Rooms';
	sidePanel.appendChild(roomsHeading);

	var addRoomButton = document.createElement('button');
	addRoomButton.classList.add('add');
	addRoomButton.textContent = '+';
	addRoomButton.addEventListener('click', function() {
		var name = promptName('room', rooms);
		if (name === null) {
			return;
		}

		dirty = true;
		lastModified['df-ai-blueprints/rooms/instances/' + name + '/default.json'] = new Date();
		lastModified['df-ai-blueprints/rooms/templates/' + name + '/default.json'] = new Date();

		gtag('event', 'dfai_create_room', {
			room_name: name
		});

		rooms[name] = {
			instances: {'default': {}},
			templates: {'default': {}}
		};
		addRoom(name);
		addRoomInstance(name, 'default');
		addRoomTemplate(name, 'default');

		findRoomTemplate(name, 'default').querySelector('a').click();
	}, false);
	roomsHeading.appendChild(addRoomButton);

	var roomList = document.createElement('ul');
	roomList.id = 'rooms';
	sidePanel.appendChild(roomList);

	var lastSelectedPlan = null;
	var selectedPlan = null;
	var selectedRoom = null;
	var selectedInstance = null;
	var selectedTemplate = null;

	window.setTags = function setTags(tags) {
		lastTags = tags;
		roomAndTagDataList.innerHTML = roomDataList.innerHTML;
		tags.forEach(function(tag) {
			if (!Object.prototype.hasOwnProperty.call(rooms, tag)) {
				var option = document.createElement('option');
				option.value = tag;
				roomAndTagDataList.appendChild(option);
			}
		});
	};

	window.roomsForTag = function roomsForTag(tag) {
		var roomNames = [tag];
		if (lastSelectedPlan) {
			var planName = lastSelectedPlan.getAttribute('data-plan-name');
			if (Object.prototype.hasOwnProperty.call(plans, planName)) {
				var plan = plans[planName];
				if (Object.prototype.hasOwnProperty.call(plan, 'tags') && Object.prototype.hasOwnProperty.call(plan.tags, tag)) {
					roomNames = plan.tags[tagName];
				}
			}
		}

		var r = [];

		roomNames.forEach(function(roomName) {
			if (Object.prototype.hasOwnProperty.call(rooms, roomName)) {
				Object.keys(rooms[roomName].templates).forEach(function(tmpl) {
					Object.keys(rooms[roomName].instances).forEach(function(inst) {
						r.push({
							roomName: roomName,
							instName: inst,
							tmplName: tmpl,
							inst: rooms[roomName].instances[inst],
							tmpl: rooms[roomName].templates[tmpl]
						});
					});
				});
			}
		});

		return r;
	};

	function findPlan(name) {
		return [].find.call(document.querySelectorAll('#plans [data-plan-name]'), function(p) {
			return p.getAttribute('data-plan-name') === name;
		});
	}

	function deletePlan(name) {
		if (confirm('Are you sure you want to delete the "' + name + '" plan?')) {
			dirty = true;
			delete lastModified['df-ai-blueprints/plans/' + name + '.json'];
			gtag('event', 'dfai_delete_plan', {
				plan_name: name
			});
			delete plans[name];
			var li = findPlan(name);
			if (lastSelectedPlan == li) {
				lastSelectedPlan = null;
				setTags([]);
			}
			if (selectedPlan === li) {
				selectedPlan = null;
				mainPanel.innerHTML = '';
			}
			li.parentNode.removeChild(li);
		}
	}

	function addPlan(name) {
		var li = document.createElement('li');
		li.setAttribute('data-plan-name', name);
		planList.appendChild(li);

		var del = document.createElement('button');
		del.classList.add('del');
		del.innerHTML = '&times;';
		del.addEventListener('click', function() {
			deletePlan(name);
		}, false);
		li.appendChild(del);

		var a = document.createElement('a');
		a.href = '#';
		a.textContent = name;
		a.addEventListener('click', function(e) {
			e.preventDefault();
			if (selectedPlan) {
				selectedPlan.classList.remove('in-editor');
				selectedPlan = null;
			}
			if (selectedRoom) {
				selectedInstance.classList.remove('in-editor');
				selectedTemplate.classList.remove('in-editor');
				selectedRoom = null;
				selectedInstance = null;
				selectedTemplate = null;
			}
			lastSelectedPlan = li;
			selectedPlan = li;
			li.classList.add('in-editor');
			mainPanel.innerHTML = '';
			openPlanEditor(mainPanel, name);
		}, false);
		li.appendChild(a);
	}

	function deleteRoom(name) {
		if (confirm('Are you sure you want to delete the "' + name + '" room?')) {
			dirty = true;
			Object.keys(rooms[name].instances).forEach(function(inst) {
				delete lastModified['df-ai-blueprints/rooms/instances/' + name + '/' + inst + '.json'];
			});
			Object.keys(rooms[name].templates).forEach(function(tmpl) {
				delete lastModified['df-ai-blueprints/rooms/templates/' + name + '/' + tmpl + '.json'];
			});
			gtag('event', 'dfai_delete_room', {
				room_name: name,
				room_inst: Object.keys(rooms[name].instances),
				room_tmpl: Object.keys(rooms[name].templates)
			});
			delete rooms[name];
			var li = findRoom(name);
			if (selectedRoom === li) {
				selectedRoom = null;
				selectedInstance = null;
				selectedTemplate = null;
				mainPanel.innerHTML = '';
			}
			li.parentNode.removeChild(li);

			roomDataList.querySelectorAll('option').forEach(function(option) {
				if (option.value === name) {
					roomDataList.removeChild(option);
				}
			});
			if (lastTags.indexOf(name) !== -1) {
				roomAndTagDataList.querySelectorAll('option').forEach(function(option) {
					if (option.value === name) {
						roomAndTagDataList.removeChild(option);
					}
				});
			}
		}
	}

	function deleteRoomInstance(name, inst) {
		if (confirm('Are you sure you want to delete the "' + inst + '" instance of the "' + name + '" room?')) {
			dirty = true;
			delete lastModified['df-ai-blueprints/rooms/instances/' + name + '/' + inst + '.json'];
			gtag('event', 'dfai_delete_room_instance', {
				room_name: name,
				room_inst: inst
			});
			delete rooms[name].instances[inst];
			var li = findRoomInstance(name, inst);
			if (selectedInstance === li) {
				selectedTemplate.classList.remove('in-editor');
				selectedRoom = null;
				selectedInstance = null;
				selectedTemplate = null;
				mainPanel.innerHTML = '';
			}
			li.parentNode.removeChild(li);
		}
	}

	function deleteRoomTemplate(name, tmpl) {
		if (confirm('Are you sure you want to delete the "' + tmpl + '" template of the "' + name + '" room?')) {
			dirty = true;
			delete lastModified['df-ai-blueprints/rooms/templates/' + name + '/' + tmpl + '.json'];
			gtag('event', 'dfai_delete_room_template', {
				room_name: name,
				room_tmpl: tmpl
			});
			delete rooms[name].templates[tmpl];
			var li = findRoomTemplate(name, tmpl);
			if (selectedTemplate === li) {
				selectedInstance.classList.remove('in-editor');
				selectedRoom = null;
				selectedInstance = null;
				selectedTemplate = null;
				mainPanel.innerHTML = '';
			}
			li.parentNode.removeChild(li);
		}
	}

	function findRoom(name) {
		return [].find.call(document.querySelectorAll('#rooms [data-room-name]'), function(r) {
			return r.getAttribute('data-room-name') === name;
		});
	}

	function findRoomInstance(name, inst) {
		return [].find.call(findRoom(name).querySelectorAll('[data-instance-name]'), function(i) {
			return i.getAttribute('data-instance-name') === inst;
		});
	}

	function findRoomTemplate(name, tmpl) {
		return [].find.call(findRoom(name).querySelectorAll('[data-template-name]'), function(t) {
			return t.getAttribute('data-template-name') === tmpl;
		});
	}

	function addRoom(name) {
		var option = document.createElement('option');
		option.value = name;
		roomDataList.appendChild(option);

		if (lastTags.indexOf(name) === -1) {
			option = document.createElement('option');
			option.value = name;
			roomAndTagDataList.appendChild(option);
		}

		var instanceList = document.createElement('datalist');
		instanceList.id = 'room-instance-list-' + name;
		instanceDataLists.appendChild(instanceList);

		var li = document.createElement('li');
		li.setAttribute('data-room-name', name);
		roomList.appendChild(li);

		var del = document.createElement('button');
		del.classList.add('del');
		del.innerHTML = '&times;';
		del.addEventListener('click', function() {
			deleteRoom(name);
			if (!Object.prototype.hasOwnProperty.call(rooms, name)) {
				instanceDataLists.removeChild(instanceList);
			}
		}, false);
		li.appendChild(del);

		var b = document.createElement('b');
		b.textContent = name;
		li.appendChild(b);

		li.appendChild(document.createElement('br'));
		var ii = document.createElement('i');
		ii.textContent = 'instances';
		li.appendChild(ii);

		var addInstanceButton = document.createElement('button');
		addInstanceButton.classList.add('add');
		addInstanceButton.textContent = '+';
		addInstanceButton.addEventListener('click', function() {
			var inst = promptName('room instance', rooms[name].instances);
			if (inst === null) {
				return;
			}

			dirty = true;
			lastModified['df-ai-blueprints/rooms/instances/' + name + '/' + inst + '.json'] = new Date();
			gtag('event', 'dfai_create_room_instance', {
				room_name: name,
				room_inst: inst
			});
			rooms[name].instances[inst] = {};
			addRoomInstance(name, inst);
			findRoomInstance(name, inst).querySelector('a').click();
		}, false);
		li.appendChild(addInstanceButton);

		var iul = document.createElement('ul');
		iul.classList.add('instances');
		li.appendChild(iul);

		var ti = document.createElement('i');
		ti.textContent = 'templates';
		li.appendChild(ti);

		var addTemplateButton = document.createElement('button');
		addTemplateButton.classList.add('add');
		addTemplateButton.textContent = '+';
		addTemplateButton.addEventListener('click', function() {
			var tmpl = promptName('room template', rooms[name].templates);
			if (tmpl === null) {
				return;
			}

			dirty = true;
			lastModified['df-ai-blueprints/rooms/templates/' + name + '/' + tmpl + '.json'] = new Date();
			gtag('event', 'dfai_create_room_template', {
				room_name: name,
				room_tmpl: tmpl
			});
			rooms[name].templates[tmpl] = {};
			addRoomTemplate(name, tmpl);
			findRoomTemplate(name, tmpl).querySelector('a').click();
		}, false);
		li.appendChild(addTemplateButton);

		var tul = document.createElement('ul');
		tul.classList.add('templates');
		li.appendChild(tul);
	}

	function addRoomInstance(name, inst) {
		var room = findRoom(name);
		var ul = room.querySelector('.instances');

		var instanceList = document.getElementById('room-instance-list-' + name);
		var instanceEntry = document.createElement('option');
		instanceEntry.value = inst;
		instanceList.appendChild(instanceEntry);

		var li = document.createElement('li');
		li.setAttribute('data-instance-name', inst);
		ul.appendChild(li);

		var del = document.createElement('button');
		del.classList.add('del');
		del.innerHTML = '&times;';
		del.addEventListener('click', function() {
			deleteRoomInstance(name, inst);
			if (!Object.prototype.hasOwnProperty.call(rooms[name].instances, inst)) {
				instanceList.removeChild(instanceEntry);
			}
		}, false);
		li.appendChild(del);

		var a = document.createElement('a');
		a.href = '#';
		a.textContent = inst;
		a.addEventListener('click', function(e) {
			e.preventDefault();
			if (selectedPlan) {
				selectedPlan.classList.remove('in-editor');
				selectedPlan = null;
			}
			if (selectedRoom === room) {
				selectedInstance.classList.remove('in-editor');
				selectedInstance = null;
			} else if (selectedRoom) {
				selectedInstance.classList.remove('in-editor');
				selectedTemplate.classList.remove('in-editor');
				selectedRoom = null;
				selectedInstance = null;
				selectedTemplate = null;
			}
			if (!selectedRoom) {
				selectedRoom = room;
				selectedTemplate = room.querySelector('[data-template-name]');
				if (!selectedTemplate) {
					dirty = true;
					lastModified['df-ai-blueprints/rooms/templates/' + name + '/default.json'] = new Date();
					rooms[name].templates['default'] = {};
					addRoomTemplate(name, 'default');
					selectedTemplate = findRoomTemplate(name, 'default');
				}
				selectedTemplate.classList.add('in-editor');
			}
			selectedInstance = li;
			mainPanel.innerHTML = '';
			li.classList.add('in-editor');
			openRoomEditor(mainPanel, name, inst, selectedTemplate.getAttribute('data-template-name'));
		}, false);
		li.appendChild(a);
	}

	function addRoomTemplate(name, tmpl) {
		var room = findRoom(name);
		var ul = room.querySelector('.templates');

		var li = document.createElement('li');
		li.setAttribute('data-template-name', tmpl);
		ul.appendChild(li);

		var del = document.createElement('button');
		del.classList.add('del');
		del.innerHTML = '&times;';
		del.addEventListener('click', function() {
			deleteRoomTemplate(name, tmpl);
		}, false);
		li.appendChild(del);

		var a = document.createElement('a');
		a.href = '#';
		a.textContent = tmpl;
		a.addEventListener('click', function(e) {
			e.preventDefault();
			if (selectedPlan) {
				selectedPlan.classList.remove('in-editor');
				selectedPlan = null;
			}
			if (selectedRoom === room) {
				selectedTemplate.classList.remove('in-editor');
				selectedTemplate = null;
			} else if (selectedRoom) {
				selectedInstance.classList.remove('in-editor');
				selectedTemplate.classList.remove('in-editor');
				selectedRoom = null;
				selectedInstance = null;
				selectedTemplate = null;
			}
			if (!selectedRoom) {
				selectedRoom = room;
				selectedInstance = room.querySelector('[data-instance-name]');
				if (!selectedInstance) {
					dirty = true;
					lastModified['df-ai-blueprints/rooms/instances/' + name + '/default.json'] = new Date();
					rooms[name].instances['default'] = {};
					addRoomInstance(name, 'default');
					selectedInstance = findRoomInstance(name, 'default');
				}
				selectedInstance.classList.add('in-editor');
			}
			selectedTemplate = li;
			mainPanel.innerHTML = '';
			li.classList.add('in-editor');
			openRoomEditor(mainPanel, name, selectedInstance.getAttribute('data-instance-name'), tmpl);
		}, false);
		li.appendChild(a);
	}

	window.resetUI = function resetUI() {
		lastSelectedPlan = null;
		selectedPlan = null;
		selectedRoom = null;
		selectedInstance = null;
		selectedTemplate = null;
		mainPanel.innerHTML = '';

		planList.innerHTML = '';
		Object.keys(plans).forEach(function(name) {
			addPlan(name);
		});

		lastTags = [];
		roomDataList.innerHTML = '';
		roomAndTagDataList.innerHTML = '';
		instanceDataLists.innerHTML = '';

		roomList.innerHTML = '';
		Object.keys(rooms).forEach(function(name) {
			addRoom(name);

			Object.keys(rooms[name].instances).forEach(function(inst) {
				addRoomInstance(name, inst);
			});
			Object.keys(rooms[name].templates).forEach(function(tmpl) {
				addRoomTemplate(name, tmpl);
			});
		});
	};

	document.getElementById('loading').style.display = 'none';
})();
