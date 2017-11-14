(function() {
	window.openPlanEditor = function openPlanEditor(mainPanel, name) {
		function markPlanDirty() {
			dirty = true;
			lastModified['df-ai-blueprints/plans/' + name + '.json'] = new Date();
		}

		gtag('event', 'dfai_edit_plan', {
			plan_name: name
		});

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
			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'max_retries',
				event_label: 'max_retries',
				mode: 'edit'
			});

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
			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'max_failures',
				event_label: 'max_failures',
				mode: 'edit'
			});

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
			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'padding_x',
				event_label: 'padding_x',
				mode: 'edit'
			});

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
			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'padding_x',
				event_label: 'padding_x',
				mode: 'edit'
			});

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
			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'padding_y',
				event_label: 'padding_y',
				mode: 'edit'
			});

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
			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'padding_y',
				event_label: 'padding_y',
				mode: 'edit'
			});

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

			var tag = promptName('tag', plan.tags);
			if (tag === null) {
				return;
			}

			markPlanDirty();
			plan.tags[tag] = [];

			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'tags',
				event_label: 'tags',
				mode: 'create_category'
			});

			setTags(Object.keys(plan.tags));
			addTag(tag);
		}, false);
		tagsHeading.appendChild(addTagButton);

		var tagList = document.createElement('div');
		mainPanel.appendChild(tagList);

		function addTag(tag) {
			var tagData = plan.tags[tag];
			var tagField = document.createElement('div');
			tagField.classList.add('field');
			tagList.appendChild(tagField);

			var tagLabel = document.createElement('label');
			tagLabel.setAttribute('for', 'edit-tag-value-' + tag);
			tagLabel.textContent = tag;
			tagField.appendChild(tagLabel);

			var del = document.createElement('button');
			del.classList.add('del');
			del.innerHTML = '&times;';
			del.addEventListener('click', function() {
				if (confirm('Are you sure you want to delete the "' + tag + '" tag?')) {
					markPlanDirty();
					tagList.removeChild(tagField);

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'tags',
						event_label: 'tags',
						mode: 'delete_category'
					});

					delete plan.tags[tag];
					setTags(Object.keys(plan.tags));
				}
			}, false);
			tagLabel.appendChild(del);

			var newTag = document.createElement('input');
			newTag.id = 'edit-tag-value-' + tag;
			newTag.type = 'text';
			newTag.setAttribute('list', 'rooms-list');
			newTag.setAttribute('placeholder', 'add room');
			newTag.addEventListener('change', function() {
				if (newTag.value.length) {
					tagData.push(newTag.value);

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'tags',
						event_label: 'tags',
						mode: 'create_element'
					});

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
					markPlanDirty();
					if (t.value.length) {
						gtag('event', 'dfai_edit_plan_field', {
							plan_name: name,
							field: 'tags',
							event_label: 'tags',
							mode: 'edit_element'
						});

						tagData[i] = t.value;
						return;
					}

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'tags',
						event_label: 'tags',
						mode: 'delete_element'
					});

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

			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'start',
				event_label: 'start',
				mode: 'edit'
			});

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

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'outdoor',
					event_label: 'outdoor',
					mode: 'create_element'
				});

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

				markPlanDirty();

				if (t.value.length) {
					plan.outdoor[i] = r.value;

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'outdoor',
						event_label: 'outdoor',
						mode: 'edit_element'
					});

					return;
				}

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'outdoor',
					event_label: 'outdoor',
					mode: 'delete_element'
				});

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

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'limits',
					event_label: 'limits',
					mode: 'delete_element'
				});

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

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'limits',
					event_label: 'limits',
					mode: 'edit_element'
				});

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
			if (newRoomLimit.value && !Object.prototype.hasOwnProperty.call(plan.limits, newRoomLimit.value)) {
				markPlanDirty();
				plan.limits = plan.limits || {};

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'limits',
					event_label: 'limits',
					mode: 'create_element'
				});

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

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'instance_limits',
					event_label: 'instance_limits',
					mode: 'delete_category'
				});

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
				if (newInstanceLimit.value && !Object.prototype.hasOwnProperty.call(limits, newInstanceLimit.value)) {
					markPlanDirty();

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'instance_limits',
						event_label: 'instance_limits',
						mode: 'create_element'
					});

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

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'instance_limits',
						event_label: 'instance_limits',
						mode: 'delete_element'
					});

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

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'instance_limits',
						event_label: 'instance_limits',
						mode: 'edit_element'
					});

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
			if (newRoomInstanceLimit.value && (!plan.instance_limits || !Object.prototype.hasOwnProperty.call(plan.instance_limits, newRoomInstanceLimit.value))) {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'instance_limits',
					event_label: 'instance_limits',
					mode: 'create_category'
				});

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

			gtag('event', 'dfai_edit_plan_field', {
				plan_name: name,
				field: 'variables',
				event_label: 'variables',
				mode: 'create_element'
			});

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

					gtag('event', 'dfai_edit_plan_field', {
						plan_name: name,
						field: 'variables',
						event_label: 'variables',
						mode: 'delete_element'
					});

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

				gtag('event', 'dfai_edit_plan_field', {
					plan_name: name,
					field: 'variables',
					event_label: 'variables',
					mode: 'create_element'
				});

				plan.variables[variable] = varValue.value;
			}, false);
			varValue.appendChild(varValue);
		}

		if (plan.variables) {
			Object.keys(plan.variables).forEach(function(variable){
				addVariable(variable);
			});
		}

		var planName = name;

		var uniquePriorityID = 0;

		function makeRoomFilter(parent, get, set) {
			var obj = get() || {};
			set(obj);

			var el = makeEnumProperty('Status', parent, enums.room_status, 'status', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Type', parent, enums.room_type, 'type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Corridor type', parent, enums.corridor_type, 'corridor_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Farm type', parent, enums.farm_type, 'farm_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Stockpile type', parent, enums.stockpile_type, 'stockpile_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Noble room type', parent, enums.nobleroom_type, 'nobleroom_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Outpost type', parent, enums.outpost_type, 'outpost_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Location type', parent, enums.location_type, 'location_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Cistern type', parent, enums.cistern_type, 'cistern_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Workshop type', parent, enums.workshop_type, 'workshop_type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Furnace type', parent, enums.furnace_type, 'furnace_type', obj);

			parent.appendChild(document.createElement('br'));

			makeStringProperty('Custom type', parent, 'raw_type', obj);

			parent.appendChild(document.createElement('br'));

			makeStringProperty('Comment', parent, 'comment', obj);

			parent.appendChild(document.createElement('br'));

			makeCountProperty('Access path', parent, makeRoomFilter, 'accesspath', obj);

			parent.appendChild(document.createElement('br'));

			makeCountProperty('Layout', parent, makeFurnitureFilter, 'layout', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Has owner', parent, 'has_owner', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Has squad', parent, 'has_squad', obj);

			parent.appendChild(document.createElement('br'));

			makeBetweenProperty('Level', parent, 'level', false, obj);

			parent.appendChild(document.createElement('br'));

			makeFilterProperty('Associated workshop', parent, makeRoomFilter, 'workshop', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumSetProperty('Disabled stockpile items', parent, enums.stockpile_list, 'stock_disable', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Stockpile-specific A', parent, 'stock_specific1', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Stockpile-specific B', parent, 'stock_specific2', obj);

			parent.appendChild(document.createElement('br'));

			makeBetweenProperty('Current users', parent, 'users', true, obj);

			parent.appendChild(document.createElement('br'));

			makeBetweenProperty('Maximum users', parent, 'has_users', true, obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Furnished', parent, 'furnished', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Queued for digging', parent, 'queue_dig', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Temporary', parent, 'temporary', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Outdoor', parent, 'outdoor', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Channeled', parent, 'channeled', obj);

			return el;
		}

		function makeFurnitureFilter(parent, get, set) {
			var obj = get() || {};
			set(obj);

			var el = makeEnumProperty('Type', parent, enums.layout_type, 'type', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Construction', parent, enums.construction_type, 'construction', obj);

			parent.appendChild(document.createElement('br'));

			makeEnumProperty('Dig', parent, enums.tile_dig_designation, 'dig', obj);

			parent.appendChild(document.createElement('br'));

			makeFilterProperty('Target', parent, makeFurnitureFilter, 'target', obj);

			parent.appendChild(document.createElement('br'));

			makeBetweenProperty('Current users', parent, 'users', true, obj);

			parent.appendChild(document.createElement('br'));

			makeBetweenProperty('Maximum users', parent, 'has_users', true, obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Ignore', parent, 'ignore', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Make room', parent, 'makeroom', obj);

			parent.appendChild(document.createElement('br'));

			makeBoolProperty('Internal', parent, 'internal', obj);

			parent.appendChild(document.createElement('br'));

			makeStringProperty('Comment', parent, 'comment', obj);

			return el;
		}

		function makePropertyVector(labelText, parent, filter, name, obj, field, isMain) {
			var index = [];

			var mainLabel = document.createElement('label');
			mainLabel.textContent = labelText;
			parent.appendChild(mainLabel);

			var addButton = document.createElement('button');
			addButton.classList.add('add');
			addButton.textContent = '+';
			addButton.addEventListener('click', function() {
				obj[name] = obj[name] || [];
				obj[name].push(undefined);

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: field,
					event_label: field,
					mode: 'create'
				});

				markPlanDirty();
				add().focus();
			}, false);
			mainLabel.appendChild(addButton);
			mainLabel.classList.add('empty');

			var last = mainLabel;

			function add() {
				var state = {
					el: document.createElement('div'),
					i: index.length
				};
				index.push(state);
				state.el.classList.add('field', 'thin');
				while (last && last.parentNode != parent) {
					last = last.parentNode;
				}
				parent.insertBefore(state.el, last);
				mainLabel.classList.remove('empty');
				if (last) {
					parent.insertBefore(last, state.el);
				}
				last = state.el;

				var label = document.createElement('label');
				state.el.appendChild(label);

				var del = document.createElement('button');
				del.classList.add('del');
				del.innerHTML = '&times;';
				del.addEventListener('click', function() {
					markPlanDirty();

					gtag('event', 'dfai_edit_plan_priority_field', {
						plan_name: planName,
						field: field,
						event_label: field,
						mode: 'delete'
					});

					index.forEach(function(s) {
						if (s.i > state.i) {
							s.i--;
						}
					});
					index.splice(state.i, 1);

					obj[name].splice(state.i, 1);
					if (index.length === 0) {
						delete obj[name];
						mainLabel.classList.add('empty');
					}

					if (state.el === last) {
						last = state.el.previousSibling;
					}
					parent.removeChild(state.el);
				}, false);
				label.appendChild(del);

				var wrapper = document.createElement('div');
				wrapper.classList.add('field');
				state.el.appendChild(wrapper);

				if (isMain) {
					state.el.classList.add('inactive');
					state.el.addEventListener('focus', function() {
						state.el.classList.remove('inactive');
					}, true);
					state.el.addEventListener('blur', function() {
						state.el.classList.add('inactive');
					}, true);
				}

				return filter(wrapper, function() {
					return obj[name][state.i];
				}, function(value) {
					if (obj[name][state.i] !== value) {
						markPlanDirty();

						gtag('event', 'dfai_edit_plan_priority_field', {
							plan_name: planName,
							field: field,
							event_label: field,
							mode: 'edit'
						});
					}
					obj[name][state.i] = value;
				}, state.el, field);
			}

			var firstElement = addButton;

			if (Object.prototype.hasOwnProperty.call(obj, name)) {
				if (!Array.isArray(obj[name])) {
					obj[name] = [obj[name]];
				}
				obj[name].forEach(function() {
					var el = add();
					if (firstElement === addButton) {
						firstElement = el;
					}
				});
			}

			return firstElement;
		}

		function makeStringFilter(parent, get, set, field) {
			var input = document.createElement('input');
			input.type = 'text';
			input.value = get() || '';
			input.addEventListener('change', function() {
				set(input.value);
			}, false);

			parent.appendChild(input);

			return input;
		}

		function makeStringProperty(labelText, parent, name, obj, field) {
			var add = makePropertyVector(labelText + ' (allowed)', parent, makeStringFilter, name, obj, field + '/' + name);

			parent.appendChild(document.createElement('br'));

			makePropertyVector(labelText + ' (forbidden)', parent, makeStringFilter, name + '_not', obj, field + '/' + name + '_not');

			return add;
		}

		function makeCountProperty(labelText, parent, filter, name, obj, field) {
			return makePropertyVector(labelText, parent, function(parent, get, set, field) {
				var obj = get() || {};
				set(obj);

				var match = makePropertyVector('Match', parent, filter, 'match', obj, field + '/match');

				parent.appendChild(document.createElement('br'));

				makeBetweenProperty('Count is', parent, 'is', true, obj, field);

				return match;
			}, name, obj, field + '/' + name);
		}

		function makeFilterProperty(labelText, parent, filter, name, obj, field) {
			var add = makePropertyVector(labelText + ' (allowed)', parent, filter, name, obj, field + '/' + name);

			parent.appendChild(document.createElement('br'));

			makePropertyVector(labelText + ' (forbidden)', parent, filter, name + '_not', obj, field + '/' + name + '_not');

			return add;
		}

		function makeBetweenProperty(labelText, parent, name, unsigned, obj, field) {
			if (Object.prototype.hasOwnProperty.call(obj, name) && !Array.isArray(obj[name])) {
				obj[name] = [obj[name], obj[name]];
			}
			var id = uniquePriorityID++;

			var label = document.createElement('label');
			label.textContent = labelText;
			label.setAttribute('for', 'edit-priorities-' + id);
			parent.appendChild(label);

			var hasMin = document.createElement('input');
			hasMin.type = 'checkbox';
			hasMin.checked = Object.prototype.hasOwnProperty.call(obj, name) && obj[name][0] !== null;

			var min = document.createElement('input');
			min.type = 'number';
			min.id = 'edit-priorities-' + id;
			if (unsigned) {
				min.min = 0;
			}
			min.value = Object.prototype.hasOwnProperty.call(obj, name) ? obj[name][0] : '';
			min.disabled = !hasMin.checked;

			var hasMax = document.createElement('input');
			hasMax.type = 'checkbox';
			hasMax.checked = Object.prototype.hasOwnProperty.call(obj, name) && obj[name][1] !== null;

			var max = document.createElement('input');
			max.type = 'number';
			if (unsigned) {
				max.min = 0;
			}
			max.value = Object.prototype.hasOwnProperty.call(obj, name) ? obj[name][1] : '';
			max.disabled = !hasMax.checked;

			var container = document.createElement('span');
			parent.appendChild(container);

			container.appendChild(document.createTextNode('between '));
			container.appendChild(hasMin);
			container.appendChild(min);
			container.appendChild(document.createTextNode(' and '));
			container.appendChild(hasMax);
			container.appendChild(max);

			function setEmpty() {
				label.classList.add('empty');
				container.classList.add('empty');
			}
			function clearEmpty() {
				label.classList.remove('empty');
				container.classList.remove('empty');
			}

			if (!hasMin.checked && !hasMax.checked) {
				setEmpty();
			}

			hasMin.addEventListener('change', function() {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: field + '/' + name,
					event_label: field + '/' + name,
					mode: 'edit'
				});

				min.disabled = !hasMin.checked;
				if (hasMin.checked) {
					obj[name] = obj[name] || [null, null];
					obj[name][0] = isNaN(min.value) ? 0 : parseInt(min.value, 10);
					clearEmpty();
				} else {
					min.value = '';
					obj[name][0] = null;
					if (obj[name][1] === null) {
						delete obj[name];
						setEmpty();
					}
				}
			}, false);

			min.addEventListener('change', function() {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: field + '/' + name,
					event_label: field + '/' + name,
					mode: 'edit'
				});

				obj[name][0] = isNaN(min.value) ? 0 : parseInt(min.value, 10);
				if (obj[name][1] !== null && obj[name][0] > obj[name][1]) {
					var swap = obj[name][0];
					obj[name][0] = obj[name][1];
					obj[name][1] = swap;
					min.value = obj[name][0];
					max.value = obj[name][1];
				}
			}, false);

			hasMax.addEventListener('change', function() {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: field + '/' + name,
					event_label: field + '/' + name,
					mode: 'edit'
				});

				max.disabled = !hasMax.checked;
				if (hasMax.checked) {
					obj[name] = obj[name] || [null, null];
					obj[name][1] = isNaN(max.value) ? (obj[name][0] || 0) : parseInt(max.value, 10);
					clearEmpty();
				} else {
					max.value = '';
					obj[name][1] = null;
					if (obj[name][0] === null) {
						delete obj[name];
						setEmpty();
					}
				}
			}, false);

			max.addEventListener('change', function() {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: field + '/' + name,
					event_label: field + '/' + name,
					mode: 'edit'
				});

				obj[name][1] = isNaN(max.value) ? (obj[name][0] || 0) : parseInt(max.value, 10);
				if (obj[name][0] !== null && obj[name][0] > obj[name][1]) {
					var swap = obj[name][0];
					obj[name][0] = obj[name][1];
					obj[name][1] = swap;
					min.value = obj[name][0];
					max.value = obj[name][1];
				}
			}, false);

			return hasMin;
		}

		function makeEnum(values) {
			return function(parent, get, set, field) {
				var select = document.createElement('select');

				var lastGroup = undefined;
				var group = select;

				values.forEach(function(val) {
					var option = document.createElement('option');
					option.value = val.e;
					option.textContent = val.n;
					if (lastGroup !== val.c) {
						if (val.c === undefined) {
							group = select;
						} else {
							group = document.createElement('optgroup');
							group.label = val.c;
							select.appendChild(group);
						}
						lastGroup = val.c;
					}
					group.appendChild(option);
				});

				select.value = get();
				select.addEventListener('change', function() {
					set(select.value);
				}, false);

				parent.appendChild(select);

				return select;
			};
		}

		function makeEnumProperty(labelText, parent, values, name, obj, field) {
			var e = makeEnum(values);

			var add = makePropertyVector(labelText + ' (allowed)', parent, e, name, obj, field + '/' + name);

			parent.appendChild(document.createElement('br'));

			makePropertyVector(labelText + ' (forbidden)', parent, e, name + '_not', obj, field + '/' + name + '_not');

			return add;
		}

		function makeEnumSetProperty(labelText, parent, values, name, obj, field) {
			var e = makeEnum(values);

			var add = makePropertyVector(labelText + ' (required)', parent, e, name, obj, field + '/' + name);

			parent.appendChild(document.createElement('br'));

			makePropertyVector(labelText + ' (forbidden)', parent, e, name + '_not', obj, field + '/' + name + '_not');

			return add;
		}

		function makeBoolProperty(labelText, parent, name, obj, field) {
			var id = uniquePriorityID++;

			var label = document.createElement('label');
			label.setAttribute('for', 'priority-edit-' + id);
			label.textContent = labelText;
			parent.appendChild(label);

			var select = document.createElement('select');
			select.id = 'priority-edit-' + id;

			var option = document.createElement('option');
			option.value = '';
			option.textContent = 'Ignore';
			select.appendChild(option);

			option = document.createElement('option');
			option.value = 'true';
			option.textContent = 'Yes';
			select.appendChild(option);

			option = document.createElement('option');
			option.value = 'false';
			option.textContent = 'No';
			select.appendChild(option);

			if (Object.prototype.hasOwnProperty.call(obj, name)) {
				select.value = Boolean(obj[name]).toString();
			} else {
				select.value = '';
				label.classList.add('empty');
				select.classList.add('empty');
			}
			select.addEventListener('change', function() {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: field + '/' + name,
					event_label: field + '/' + name,
					mode: 'edit'
				});

				switch (select.value) {
					case 'true':
						obj[name] = true;
						label.classList.remove('empty');
						select.classList.remove('empty');
						break;
					case 'false':
						obj[name] = false;
						label.classList.remove('empty');
						select.classList.remove('empty');
						break;
					default:
						delete obj[name];
						label.classList.add('empty');
						select.classList.add('empty');
						break;
				}
			}, false);
			parent.appendChild(select);

			return select;
		}

		function makePriority(parent, get, set, field) {
			var priority = get() || {};
			set(priority);

			var id = uniquePriorityID++;

			var label = document.createElement('label');
			label.setAttribute('for', 'edit-priority-action-' + id);
			label.textContent = 'Action';
			parent.appendChild(label);

			var action = makeEnum(enums.plan_priority_action)(parent, function() {
				return priority.action;
			}, function(action) {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: 'priority/action',
					event_label: 'priority/action',
					mode: 'edit'
				});

				priority.action = action;
			}, 'priority/action');
			action.id = 'priority-select-action-' + id;

			parent.appendChild(document.createElement('br'));

			label = document.createElement('label');
			label.setAttribute('for', 'edit-priority-continue-' + id);
			label.textContent = 'Continue';
			parent.appendChild(label);

			var keepGoing = document.createElement('input');
			keepGoing.id = 'edit-priority-continue-' + id;
			keepGoing.type = 'checkbox';
			keepGoing.checked = Boolean(priority['continue']);
			keepGoing.addEventListener('change', function() {
				markPlanDirty();

				gtag('event', 'dfai_edit_plan_priority_field', {
					plan_name: planName,
					field: 'priority/continue',
					event_label: 'priority/continue',
					mode: 'edit'
				});

				if (keepGoing.checked) {
					priority['continue'] = true;
				} else {
					delete priority['continue'];
				}
			}, false);
			parent.appendChild(keepGoing);

			label = document.createElement('label');
			label.setAttribute('for', 'edit-priority-continue-' + id);
			label.classList.add('inline');
			label.textContent = ' don\'t stop checking for an action when this action is performed';
			parent.appendChild(label);

			parent.appendChild(document.createElement('br'));

			makeCountProperty('Required room count', parent, makeRoomFilter, 'count', priority, 'priority');

			parent.appendChild(document.createElement('br'));

			makeFilterProperty('Match rooms', parent, makeRoomFilter, 'match', priority, 'priority');

			parent.appendChild(document.createElement('br'));

			parent.appendChild(document.createElement('hr'));

			return action;
		}

		var prioritiesHeader = document.createElement('h2');
		mainPanel.appendChild(prioritiesHeader);

		var prioritiesList = document.createElement('div');
		mainPanel.appendChild(prioritiesList);

		makePropertyVector('Priorities', prioritiesList, makePriority, 'priorities', plan, 'priority', true);

		var prioritiesLabel = prioritiesList.querySelector('label');
		prioritiesList.removeChild(prioritiesLabel);
		while (prioritiesLabel.firstChild) {
			prioritiesHeader.appendChild(prioritiesLabel.firstChild);
		}
	};
})();
