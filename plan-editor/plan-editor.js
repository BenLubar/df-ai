(function() {
	var uniqueTagID = 0;

	window.openPlanEditor = function openPlanEditor(mainPanel, name) {
		function markPlanDirty() {
			dirty = true;
			lastModified['df-ai-blueprints/plans/' + name + '.json'] = new Date();
		}

		var plan = plans[name];

		var h1 = document.createElement('h1');
		h1.textContent = 'Editing plan: ' + name;
		mainPanel.appendChild(h1);

		var metaHeading = document.createElement('h2');
		metaHeading.textContent = 'Metadata';
		mainPanel.appendChild(metaHeading);

		var div = document.createElement('div');
		div.classList.add('field');
		mainPanel.appendChild(div);

		var label = document.createElement('label');
		label.textContent = 'Maximum plan retries';
		label.setAttribute('for', 'max_retries');
		div.appendChild(label);

		var max_retries = document.createElement('input');
		max_retries.id = 'max_retries';
		max_retries.type = 'number';
		max_retries.min = 1;
		max_retries.value = isNaN(plan.max_retries) ? 25 : parseInt(plan.max_retries, 10);
		max_retries.addEventListener('change', function() {
			markPlanDirty();
			plan.max_retries = isNaN(max_retries.value) ? 25 : Math.max(parseInt(max_retries.value, 10), 1);
		}, false);
		div.appendChild(max_retries);

		div = document.createElement('div');
		div.classList.add('field');
		mainPanel.appendChild(div);

		label = document.createElement('label');
		label.textContent = 'Maximum room placement failures';
		label.setAttribute('for', 'max_failures');
		div.appendChild(label);

		var max_failures = document.createElement('input');
		max_failures.id = 'max_failures';
		max_failures.type = 'number';
		max_failures.min = 1;
		max_failures.value = isNaN(plan.max_failures) ? 100 : parseInt(plan.max_failures, 10);
		max_failures.addEventListener('change', function() {
			markPlanDirty();
			plan.max_failures = isNaN(max_failures.value) ? 100 : Math.max(parseInt(max_failures.value, 10), 1);
		}, false);
		div.appendChild(max_failures);

		div = document.createElement('div');
		div.classList.add('field');
		mainPanel.appendChild(div);

		label = document.createElement('label');
		label.textContent = 'Fortress entrance padding';
		label.setAttribute('for', 'padding_x');
		div.appendChild(label);

		var padding_x_min = document.createElement('input');
		padding_x_min.type = 'number';
		padding_x_min.max = 0;
		padding_x_min.value = !plan.padding_x || isNaN(plan.padding_x[0]) ? 0 : parseInt(plan.padding_x[0], 10);
		padding_x_min.addEventListener('change', function() {
			markPlanDirty();
			plan.padding_x = [isNaN(padding_x_min.value) ? 0 : Math.min(parseInt(padding_x_min.value, 10), 0), !plan.padding_x || isNaN(plan.padding_x[1]) ? 0 : Math.max(parseInt(plan.padding_x[1], 10), 0)];
		}, false);
		div.appendChild(padding_x_min);

		div.appendChild(document.createTextNode(' to '));

		var padding_x_max = document.createElement('input');
		padding_x_max.type = 'number';
		padding_x_max.min = 0;
		padding_x_max.value = !plan.padding_x || isNaN(plan.padding_x[1]) ? 0 : parseInt(plan.padding_x[1], 10);
		padding_x_max.addEventListener('change', function() {
			markPlanDirty();
			plan.padding_x = [!plan.padding_x || isNaN(plan.padding_x[0]) ? 0 : Math.min(parseInt(plan.padding_x[0], 10), 0), isNaN(padding_x_max.value) ? 0 : Math.max(parseInt(padding_x_max.value, 10), 0)];
		}, false);
		div.appendChild(padding_x_max);

		div.appendChild(document.createTextNode(' X'));
		div.appendChild(document.createElement('br'));

		var padding_y_min = document.createElement('input');
		padding_y_min.type = 'number';
		padding_y_min.max = 0;
		padding_y_min.value = !plan.padding_y || isNaN(plan.padding_y[0]) ? 0 : parseInt(plan.padding_y[0], 10);
		padding_y_min.addEventListener('change', function() {
			markPlanDirty();
			plan.padding_y = [isNaN(padding_y_min.value) ? 0 : Math.min(parseInt(padding_y_min.value, 10), 0), !plan.padding_y || isNaN(plan.padding_y[1]) ? 0 : Math.max(parseInt(plan.padding_y[1], 10), 0)];
		}, false);
		div.appendChild(padding_y_min);

		div.appendChild(document.createTextNode(' to '));

		var padding_y_max = document.createElement('input');
		padding_y_max.type = 'number';
		padding_y_max.min = 0;
		padding_y_max.value = !plan.padding_y || isNaN(plan.padding_y[1]) ? 0 : parseInt(plan.padding_y[1], 10);
		padding_y_max.addEventListener('change', function() {
			markPlanDirty();
			plan.padding_y = [!plan.padding_y || isNaN(plan.padding_y[0]) ? 0 : Math.min(parseInt(plan.padding_y[0], 10), 0), isNaN(padding_y_max.value) ? 0 : Math.max(parseInt(padding_y_max.value, 10), 0)];
		}, false);
		div.appendChild(padding_y_max);

		div.appendChild(document.createTextNode(' Y'));

		var tagsHeading = document.createElement('h2');
		tagsHeading.textContent = 'Tags';
		mainPanel.appendChild(tagsHeading);

		var addTagButton = document.createElement('button');
		addTagButton.classList.add('add');
		addTagButton.textContent = '+';
		addTagButton.addEventListener('click', function() {
			plan.tags = plan.tags || {};

			var name = promptName('tag', plan.tags);
			if (name === null) {
				return;
			}

			markPlanDirty();
			plan.tags[name] = [];
			setTags(Object.keys(plan.tags));
			addTag(name);
		}, false);
		tagsHeading.appendChild(addTagButton);

		var tagList = document.createElement('div');
		mainPanel.appendChild(tagList);

		function addTag(tag) {
			var newTagID = 'newtag' + (uniqueTagID++);
			var tagData = plan.tags[tag];
			var tagField = document.createElement('div');
			tagField.classList.add('field');
			tagList.appendChild(tagField);

			var tagLabel = document.createElement('label');
			tagLabel.setAttribute('for', newTagID);
			tagLabel.textContent = tag;
			tagField.appendChild(tagLabel);

			var del = document.createElement('button');
			del.classList.add('del');
			del.innerHTML = '&times;';
			del.addEventListener('click', function() {
				if (confirm('Are you sure you want to delete the "' + tag + '" tag?')) {
					markPlanDirty();
					tagList.removeChild(tagField);
					delete plan.tags[tag];
					setTags(Object.keys(plan.tags));
				}
			}, false);
			tagLabel.appendChild(del);

			var newTag = document.createElement('input');
			newTag.id = newTagID;
			newTag.type = 'text';
			newTag.setAttribute('list', 'rooms-list');
			newTag.setAttribute('placeholder', 'add room');
			newTag.addEventListener('change', function() {
				if (newTag.value.length) {
					tagData.push(newTag.value);
					var t = addTagEntry(newTag.value);
					if (newTag === document.activeElement) {
						t.focus();
					}
					newTag.value = '';
				}
			}, false);
			tagField.appendChild(newTag);

			function addTagEntry(value) {
				var t = document.createElement('input');
				t.type = 'text';
				t.setAttribute('list', 'rooms-list');
				t.value = value;
				t.addEventListener('change', function() {
					if (t.value === value) {
						return;
					}
					var i = -1;
					for (var c = t; c; c = c.previousSibling) {
						if (c.nodeName === 'INPUT') {
							i++;
						}
					}
					if (t.value.length) {
						tagData[i] = t.value;
						markPlanDirty();
						return;
					}
					tagData.splice(i, 1);
					t.parentNode.removeChild(t.nextSibling);
					t.parentNode.removeChild(t);
				}, false);
				tagField.insertBefore(t, newTag);
				tagField.insertBefore(document.createElement('br'), newTag);
				return t;
			}

			tagData.forEach(function(value) {
				addTagEntry(value);
			});
		}

		setTags(plan.tags ? Object.keys(plan.tags) : []);
		if (plan.tags) {
			Object.keys(plan.tags).forEach(function(tag) {
				addTag(tag);
			});
		}

		var outdoorRoomsHeading = document.createElement('h2');
		outdoorRoomsHeading.textContent = 'Outdoor rooms';
		mainPanel.appendChild(outdoorRoomsHeading);

		div = document.createElement('div');
		div.classList.add('field');
		mainPanel.appendChild(div);

		label = document.createElement('label');
		label.textContent = 'Starting room or tag';
		label.setAttribute('for', 'start');
		div.appendChild(label);

		var start = document.createElement('input');
		start.id = 'start';
		start.type = 'text';
		start.setAttribute('list', 'rooms-and-tags-list');
		start.value = Object.prototype.hasOwnProperty.call(plan, 'start') ? plan.start : '';
		start.addEventListener('change', function() {
			markPlanDirty();
			plan.start = start.value;
		}, false);
		div.appendChild(start);

		div = document.createElement('div');
		div.classList.add('field');
		mainPanel.appendChild(div);
		var outdoorRooms = div;

		label = document.createElement('label');
		label.textContent = 'Other outdoor rooms';
		label.setAttribute('for', 'add_outdoor_room');
		div.appendChild(label);

		var newOutdoorRoom = document.createElement('input');
		newOutdoorRoom.id = 'add_outdoor_room';
		newOutdoorRoom.type = 'text';
		newOutdoorRoom.setAttribute('list', 'rooms-and-tags-list');
		newOutdoorRoom.setAttribute('placeholder', 'add room or tag');
		newOutdoorRoom.addEventListener('change', function() {
			if (newOutdoorRoom.value.length) {
				plan.outdoor = plan.outdoor || [];
				plan.outdoor.push(newOutdoorRoom.value);
				var r = addOutdoorRoomEntry(newOutdoorRoom.value);
				if (newOutdoorRoom === document.activeElement) {
					r.focus();
				}
				newOutdoorRoom.value = '';
			}
		}, false);
		div.appendChild(newOutdoorRoom);

		function addOutdoorRoomEntry(value) {
			var r = document.createElement('input');
			r.type = 'text';
			r.setAttribute('list', 'rooms-and-tags-list');
			r.value = value;
			r.addEventListener('change', function() {
				if (r.value === value) {
					return;
				}
				var i = -1;
				for (var c = r; c; c = c.previousSibling) {
					if (c.nodeName === 'INPUT') {
						i++;
					}
				}
				if (t.value.length) {
					plan.outdoor[i] = r.value;
					markPlanDirty();
					return;
				}
				plan.outdoor.splice(i, 1);
				r.parentNode.removeChild(r.nextSibling);
				r.parentNode.removeChild(r);
			}, false);
			outdoorRooms.insertBefore(r, newOutdoorRoom);
			outdoorRooms.insertBefore(document.createElement('br'), newOutdoorRoom);
			return r;
		}

		if (plan.outdoor) {
			plan.outdoor.forEach(function(value) {
				addOutdoorRoomEntry(value);
			});
		}

		var roomLimitsHeading = document.createElement('h2');
		mainPanel.appendChild(roomLimitsHeading);

		label = document.createElement('label');
		label.textContent = 'Room limits';
		label.setAttribute('for', 'room-limits-add');
		roomLimitsHeading.appendChild(label);

		var roomLimitsList = document.createElement('div');
		mainPanel.appendChild(roomLimitsList);

		function addRoomLimit(room) {
			var existing = [].find.call(roomLimitsList.querySelectorAll('[data-room-name]'), function(r) {
				return r.getAttribute('data-room-name') === room;
			});
			if (existing) {
				return existing;
			}

			var field = document.createElement('div');
			field.classList.add('field');
			field.setAttribute('data-room-name', room);
			roomLimitsList.appendChild(field);

			var label = document.createElement('label');
			var first = true;
			room.split(/_/g).forEach(function(part) {
				if (first) {
					first = false;
				} else {
					label.appendChild(document.createTextNode('_'));
					label.appendChild(document.createElement('wbr'));
				}
				label.appendChild(document.createTextNode(part));
			});
			label.setAttribute('for', 'room-limits-min-' + room);
			field.appendChild(label);

			var del = document.createElement('button');
			del.classList.add('del');
			del.innerHTML = '&times;';
			del.addEventListener('click', function() {
				markPlanDirty();
				roomLimitsList.removeChild(field);
				delete plan.limits[room];
			}, false);
			label.appendChild(del);

			var min = document.createElement('input');
			var max = document.createElement('input');
			min.type = 'number';
			max.type = 'number';
			min.min = 0;
			max.min = 1;
			min.value = isNaN(plan.limits[room][0]) ? 0 : Math.max(parseInt(plan.limits[room][0], 10), 0);
			var minMax = Math.max(parseInt(min.value, 10), 1);
			max.value = isNaN(plan.limits[room][1]) ? minMax : Math.max(parseInt(plan.limits[room][1], 10), minMax);
			min.id = 'room-limits-min-' + room;
			function change() {
				markPlanDirty();
				var minN = Math.max(parseInt(min.value, 10), 0);
				var maxN = Math.max(parseInt(max.value, 10), Math.max(minN, 1));
				plan.limits[room] = [minN, maxN];
			}
			min.addEventListener('change', change, false);
			max.addEventListener('change', change, false);
			field.appendChild(min);
			field.appendChild(document.createTextNode(' to '));
			field.appendChild(max);

			return field;
		}

		if (plan.limits) {
			Object.keys(plan.limits).forEach(function(room) {
				addRoomLimit(room);
			});
		}

		var newRoomLimit = document.createElement('input');
		newRoomLimit.type = 'text';
		newRoomLimit.id = 'room-limits-add';
		newRoomLimit.setAttribute('list', 'rooms-list');
		newRoomLimit.setAttribute('placeholder', 'add room');
		mainPanel.appendChild(newRoomLimit);

		var addButton = document.createElement('button');
		addButton.classList.add('add');
		addButton.textContent = '+';
		addButton.addEventListener('click', function() {
			if (newRoomLimit.value) {
				markPlanDirty();
				plan.limits = plan.limits || {};
				plan.limits[newRoomLimit.value] = plan.limits[newRoomLimit.value] || [1, 1];
				addRoomLimit(newRoomLimit.value).querySelector('input').focus();
				newRoomLimit.value = '';
			}
		});
		mainPanel.appendChild(addButton);

		var roomInstanceLimitsHeading = document.createElement('h2');
		mainPanel.appendChild(roomInstanceLimitsHeading);

		label = document.createElement('label');
		label.textContent = 'Room instance limits';
		label.setAttribute('for', 'room-instance-limits-add');
		roomInstanceLimitsHeading.appendChild(label);

		var roomInstanceLimitsList = document.createElement('div');
		mainPanel.appendChild(roomInstanceLimitsList);

		function addRoomInstanceLimits(room) {
			var existing = [].find.call(roomInstanceLimitsList.querySelectorAll('[data-room-name]'), function(r) {
				return r.getAttribute('data-room-name') === room;
			});
			if (existing) {
				return existing;
			}

			var list = document.createElement('div');
			list.classList.add('field');
			list.setAttribute('data-room-name', room);
			roomInstanceLimitsList.appendChild(list);

			var label = document.createElement('label');
			var first = true;
			room.split(/_/g).forEach(function(part) {
				if (first) {
					first = false;
				} else {
					label.appendChild(document.createTextNode('_'));
					label.appendChild(document.createElement('wbr'));
				}
				label.appendChild(document.createTextNode(part));
			});
			label.setAttribute('for', 'room-instance-limits-add-' + room);
			list.appendChild(label);

			var del = document.createElement('button');
			del.classList.add('del');
			del.innerHTML = '&times;';
			del.addEventListener('click', function() {
				markPlanDirty();
				roomInstanceLimitsList.removeChild(list);
				delete plan.instance_limits[room];
			}, false);
			label.appendChild(del);

			var limits = plan.instance_limits[room];

			var newInstanceLimit = document.createElement('input');
			newInstanceLimit.type = 'text';
			newInstanceLimit.id = 'room-instance-limits-add-' + room;
			newInstanceLimit.setAttribute('list', 'room-instance-list-' + room);
			newInstanceLimit.setAttribute('placeholder', 'add instance');
			list.appendChild(newInstanceLimit);

			var addButton = document.createElement('button');
			addButton.classList.add('add');
			addButton.textContent = '+';
			addButton.addEventListener('click', function() {
				if (newInstanceLimit.value) {
					markPlanDirty();
					limits[newInstanceLimit.value] = limits[newInstanceLimit.value] || [1, 1];
					addInstanceLimit(newInstanceLimit.value).querySelector('input').focus();
					newInstanceLimit.value = '';
				}
			});
			list.appendChild(addButton);

			function addInstanceLimit(inst) {
				var field = document.createElement('div');
				field.classList.add('field');
				field.setAttribute('data-instance-name', inst);
				list.insertBefore(field, newInstanceLimit);

				var label = document.createElement('label');
				var first = true;
				inst.split(/_/g).forEach(function(part) {
					if (first) {
						first = false;
					} else {
						label.appendChild(document.createTextNode('_'));
						label.appendChild(document.createElement('wbr'));
					}
					label.appendChild(document.createTextNode(part));
				});
				label.setAttribute('for', 'room-instance-limits-min-' + room + '/' + inst);
				field.appendChild(label);

				var del = document.createElement('button');
				del.classList.add('del');
				del.innerHTML = '&times;';
				del.addEventListener('click', function() {
					markPlanDirty();
					list.removeChild(field);
					delete limits[inst];
				}, false);
				label.appendChild(del);

				var min = document.createElement('input');
				var max = document.createElement('input');
				min.type = 'number';
				max.type = 'number';
				min.min = 0;
				max.min = 0;
				min.value = isNaN(limits[inst][0]) ? 0 : Math.max(parseInt(limits[inst][0], 10), 0);
				var minMax = Math.max(parseInt(min.value, 10), 1);
				max.value = isNaN(limits[inst][1]) ? minMax : Math.max(parseInt(limits[inst][1], 10), parseInt(min.value, 10));
				min.id = 'room-instance-limits-min-' + room + '/' + inst;
				function change() {
					markPlanDirty();
					var minN = Math.max(parseInt(min.value, 10), 0);
					var maxN = Math.max(parseInt(max.value, 10), minN);
					limits[inst] = [minN, maxN];
				}
				min.addEventListener('change', change, false);
				max.addEventListener('change', change, false);
				field.appendChild(min);
				field.appendChild(document.createTextNode(' to '));
				field.appendChild(max);

				return field;
			}

			Object.keys(limits).forEach(function(inst) {
				addInstanceLimit(inst);
			});

			return list;
		}

		if (plan.instance_limits) {
			Object.keys(plan.instance_limits).forEach(function(room) {
				addRoomInstanceLimits(room);
			});
		}

		var newRoomInstanceLimit = document.createElement('input');
		newRoomInstanceLimit.type = 'text';
		newRoomInstanceLimit.id = 'room-instance-limits-add';
		newRoomInstanceLimit.setAttribute('list', 'rooms-list');
		newRoomInstanceLimit.setAttribute('placeholder', 'add room');
		mainPanel.appendChild(newRoomInstanceLimit);

		addButton = document.createElement('button');
		addButton.classList.add('add');
		addButton.textContent = '+';
		addButton.addEventListener('click', function() {
			if (newRoomInstanceLimit.value) {
				markPlanDirty();
				plan.instance_limits = plan.instance_limits || {};
				plan.instance_limits[newRoomInstanceLimit.value] = plan.instance_limits[newRoomInstanceLimit.value] || {};
				addRoomInstanceLimits(newRoomInstanceLimit.value).querySelector('input').focus();
				newRoomInstanceLimit.value = '';
			}
		});
		mainPanel.appendChild(addButton);

		var variablesHeading = document.createElement('h2');
		variablesHeading.textContent = 'Variables';
		mainPanel.appendChild(variablesHeading);

		var addVariableButton = document.createElement('button');
		addVariableButton.classList.add('add');
		addVariableButton.textContent = '+';
		addVariableButton.addEventListener('click', function() {
			plan.variables = plan.variables || {};

			var name = promptName('variable', plan.variable);
			if (name === null) {
				return;
			}

			markPlanDirty();
			plan.variables[name] = '';
			addVariable(name);
		}, false);
		variablesHeading.appendChild(addVariableButton);

		var variableList = document.createElement('div');
		mainPanel.appendChild(variableList);

		function addVariable(variable) {
			var varField = document.createElement('div');
			varField.classList.add('field');
			variableList.appendChild(varField);

			var varLabel = document.createElement('label');
			varLabel.setAttribute('for', 'variable_' + variable);
			varLabel.textContent = variable;
			varField.appendChild(tagLabel);

			var del = document.createElement('button');
			del.classList.add('del');
			del.innerHTML = '&times;';
			del.addEventListener('click', function() {
				if (confirm('Are you sure you want to delete the "' + variable + '" variable?')) {
					markPlanDirty();
					variableList.removeChild(varField);
					delete plan.variables[variable];
				}
			}, false);
			varLabel.appendChild(del);

			var varValue = document.createElement('input');
			varValue.id = 'variable_' + variable;
			varValue.type = 'text';
			varValue.value = plan.variables[variable];
			varValue.addEventListener('change', function() {
				markPlanDirty();
				plan.variables[variable] = varValue.value;
			}, false);
			varValue.appendChild(varValue);
		}

		if (plan.variables) {
			Object.keys(plan.variables).forEach(function(variable){
				addVariable(variable);
			});
		}
	};
})();
