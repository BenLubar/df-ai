(function() {
	function splitVariableString(str) {
		var vstr = [];
		var esc = false;
		var next = [];
		for (var i = 0; i < str.length; i++) {
			if (esc) {
				next.push(str.charAt(i));
				esc = false;
				continue;
			}
			if (str.charAt(i) === '\\') {
				esc = true;
				continue;
			}
			if (str.charAt(i) === '$') {
				if (str.charAt(i + 1) === '{') {
					var varName = ['$'];
					for (var j = i + 2; j < str.length; j++) {
						if (esc) {
							varName.push(str.charAt(j));
							esc = false;
						} else if (str.charAt(j) === '\\') {
							esc = true;
						} else if (str.charAt(j) === '}') {
							if (next.length) {
								vstr.push(next.join(''));
								next = [];
							}
							vstr.push(varName.join(''));
							varName = null;
							i = j;
							break;
						} else {
							varName.push(str.charAt(j));
						}
					}
					if (varName) {
						if (next.length) {
							next.push('$');
						} else {
							vstr.push('$');
						}
					}
				} else if (/[a-zA-Z_]/.test(str.charAt(i + 1))) {
					if (next.length) {
						vstr.push(next.join(''));
						next = [];
					}
					var varName = /^\$[a-zA-Z0-9_]+/.exec(str.substring(i))[0];
					i += varName.length - 1;
					vstr.push(varName);
				}
			} else {
				next.push(str.charAt(i));
			}
		}
		if (esc) {
			next.push('\\');
		}
		if (next.length) {
			vstr.push(next.join(''));
		}
		return vstr;
	}
	function maybeSplitVariableString(str) {
		var vstr = splitVariableString(str);
		if (vstr.length === 1 && (vstr[0] === '$' || vstr[0].charAt(0) !== '$')) {
			return vstr[0];
		}
		return vstr;
	}
	function concatVariableString(vstr) {
		if (!Array.isArray(vstr)) {
			if (!vstr) {
				return '';
			}
			return vstr.replace(/[\$\\]/g, function(s) {
				return '\\' + s;
			});
		}
		vstr = vstr.slice(0);
		for (var i = 0; i < vstr.length; i++) {
			if (vstr[i] === '$') {
				vstr[i] = '\\$';
			}
			if (vstr[i].charAt(0) === '$') {
				if (/^\$[a-zA-Z_][a-zA-Z0-9_]+$/.test(vstr[i])) {
					if (i + 1 < vstr.length && /^[a-zA-Z0-9_]/.test(vstr[i + 1])) {
						vstr[i] = '${' + vstr[i].substring(1) + '}';
					}
				} else {
					vstr[i] = '${' + vstr[i].substring(1).replace(/[\\\{\}]/g, function(s) {
						return '\\' + s;
					}) + '}';
				}
			} else {
				vstr[i] = vstr[i].replace(/[\$\\]/g, function(s) {
					return '\\' + s;
				});
			}
		}
		return vstr.join('');
	}
	function appendVariableString(el, vstr) {
		if (!Array.isArray(vstr)) {
			el.appendChild(document.createTextNode(vstr));
			return;
		}
		vstr.forEach(function(s) {
			if (s === '$' || s.charAt(0) !== '$') {
				el.appendChild(document.createTextNode(s));
			} else {
				var i = document.createElement('i');
				i.classList.add('variable');
				i.textContent = s;
				el.appendChild(i);
			}
			el.appendChild(document.createElement('wbr'));
		});
	}

	window.openRoomEditor = function openRoomInstanceEditor(mainPanel, name, inst, tmpl) {
		function markInstanceDirty() {
			dirty = true;
			lastModified['df-ai-blueprints/rooms/instances/' + name + '/' + inst + '.json'] = new Date();
		}
		function markTemplateDirty() {
			dirty = true;
			lastModified['df-ai-blueprints/rooms/templates/' + name + '/' + tmpl + '.json'] = new Date();
		}

		var header = document.createElement('h1');
		header.textContent = 'Editing room: ' + name + ' (i:\u00a0' + inst + ', t:\u00a0' + tmpl + ')';
		mainPanel.appendChild(header);

		var room = rooms[name];
		var instance = room.instances[inst];
		var template = room.templates[tmpl];

		if (Object.prototype.hasOwnProperty.call(instance, 'p') && Array.isArray(instance.p) && instance.p.length) {
			alert('Warning: The room editor does not currently support placeholders. Editing this room may corrupt it.');
		}

		var min = [0, 0, 0];
		var max = [0, 0, 0];

		function svgEl(name) {
			return document.createElementNS('http://www.w3.org/2000/svg', name);
		}
		var svg = svgEl('svg');
		mainPanel.appendChild(svg);

		var container = document.createElement('div');
		container.classList.add('container');
		mainPanel.appendChild(container);

		var containerSide = document.createElement('div');
		containerSide.classList.add('side');
		container.appendChild(containerSide);

		var addLayout = document.createElement('label');

		var itemEditorPanel = document.createElement('div');
		itemEditorPanel.classList.add('main');
		container.appendChild(itemEditorPanel);

		var roomsG = svgEl('g');
		roomsG.classList.add('room');
		svg.appendChild(roomsG);

		var entranceG = svgEl('g');
		entranceG.classList.add('entrance');
		svg.appendChild(entranceG);

		var entranceTitle = svgEl('title');
		entranceTitle.textContent = 'Entrance';
		entranceG.appendChild(entranceTitle);

		var entranceCircle = svgEl('circle');
		entranceCircle.setAttributeNS(null, 'cx', '0.5');
		entranceCircle.setAttributeNS(null, 'cy', '0.5');
		entranceCircle.setAttributeNS(null, 'r', '0.25');
		entranceG.appendChild(entranceCircle);

		function updateEditorBounds() {
			svg.setAttributeNS(null, 'viewBox', [min[0] - 2, min[1] - 2, max[0] - min[0] + 5, max[1] - min[1] + 5].join(' '));
		}

		var selectedFile = null;
		var selectedPlaceholders = null;
		var selectedRoom = null;
		var selectedLayout = null;
		var selectedLayoutList = null;

		function markDirty() {
			if (selectedPlaceholders) {
				markTemplateDirty();
			} else {
				markInstanceDirty();
			}
		}

		function addRoom(r, file, placeholders, list) {
			var item = document.createElement('li');
			list.appendChild(item);

			var g = svgEl('g');
			roomsG.appendChild(g);

			var layoutList = document.createElement('ul');

			var a = document.createElement('a');
			a.href = '#';
			a.textContent = '[room]';
			a.addEventListener('click', function(e) {
				e.preventDefault();

				[].forEach.call(svg.querySelectorAll('.active'), function(el) {
					el.classList.remove('active');
				});
				roomsG.appendChild(g);
				g.classList.add('active');

				selectedFile = file;
				selectedPlaceholders = placeholders;
				selectedRoom = r;
				selectedLayout = null;
				if (selectedLayoutList) {
					containerSide.removeChild(selectedLayoutList);
				}
				selectedLayoutList = layoutList;
				containerSide.insertBefore(layoutList, addLayout);
				containerSide.insertBefore(addLayout, layoutList);
				updateSelected();
			}, false);
			item.appendChild(a);

			var comment = document.createElement('small');
			comment.style.display = 'none';
			item.appendChild(comment);

			function updateRoom() {
				while (g.firstChild) {
					g.removeChild(g.firstChild);
				}
				if (!r) {
					return;
				}
				var roomPath = [];
				if (placeholders && Object.prototype.hasOwnProperty.call(r, 'placeholder') && !isNaN(r.placeholder)) {
					debugger;
				}
				if (!Object.prototype.hasOwnProperty.call(r, 'min') || !Array.isArray(r.min) || r.min.length !== 3) {
					r.min = [0, 0, 0];
				}
				if (!Object.prototype.hasOwnProperty.call(r, 'max') || !Array.isArray(r.max) || r.max.length !== 3) {
					r.max = [0, 0, 0];
				}
				r.min = r.min.map(function(x) {
					return isNaN(x) ? 0 : x;
				});
				r.max = r.max.map(function(x) {
					return isNaN(x) ? 0 : x;
				});
				min[0] = Math.min(min[0], r.min[0]);
				min[1] = Math.min(min[1], r.min[1]);
				min[2] = Math.min(min[2], r.min[2]);
				max[0] = Math.max(max[0], r.max[0]);
				max[1] = Math.max(max[1], r.max[1]);
				max[2] = Math.max(max[2], r.max[2]);
				roomPath.push('M', r.min[0], ' ', r.min[1]);
				roomPath.push('h', r.max[0] - r.min[0] + 1);
				roomPath.push('v', r.max[1] - r.min[1] + 1);
				roomPath.push('h', -(r.max[0] - r.min[0] + 1));
				roomPath.push('v', -(r.max[1] - r.min[1] + 1));

				var layoutG = svgEl('g');
				layoutG.classList.add('layout');
				layoutList.innerHTML = '';

				if (Object.prototype.hasOwnProperty.call(r, 'layout') && Array.isArray(r.layout)) {
					r.layout.forEach(function(l) {
						if (isNaN(l)) {
							return;
						}
						var f = (file.f || [])[l];
						if (!f) {
							return;
						}
						if (placeholders && Object.prototype.hasOwnProperty.call(f, 'placeholder') && !isNaN(f.placeholder)) {
							debugger;
						}

						addFurniture(layoutG, f, r, file, placeholders, layoutList);

						min[0] = Math.min(min[0], r.min[0] + f.x);
						min[1] = Math.min(min[1], r.min[1] + f.y);
						min[2] = Math.min(min[2], r.min[2] + f.z);
						max[0] = Math.max(max[0], r.min[0] + f.x);
						max[1] = Math.max(max[1], r.min[1] + f.y);
						max[2] = Math.max(max[2], r.min[2] + f.z);

						if (Object.prototype.hasOwnProperty.call(f, 'dig') && f.dig === 'No') {
							roomPath.push('M', r.min[0] + f.x, ' ', r.min[1] + f.y, 'h1v1h-1v-1');
						}
					});
				}
				var rt = enums.room_type.find(function(rt) {
					return rt.e === r.type;
				});
				if (!rt) {
					a.textContent = '[room]';
				} else {
					a.textContent = rt.n;
					if (rt.hasOwnProperty('ste')) {
						var st = enums[rt.ste].find(function(st) {
							return st.e === r[rt.ste];
						});
						if (st) {
							if (st.raw && Object.prototype.hasOwnProperty.call(r, 'raw_type')) {
								a.appendChild(document.createTextNode(' ("'));
								appendVariableString(a, r.raw_type);
								a.appendChild(document.createTextNode('")'));
							} else {
								a.textContent += ' (' + st.n + ')';
							}
						}
					}
				}

				comment.innerHTML = '';
				if (Object.prototype.hasOwnProperty.call(r, 'comment')) {
					appendVariableString(comment, r.comment);
					comment.style.display = 'block';
				} else {
					comment.style.display = 'none';
				}

				var path = svgEl('path');
				path.setAttributeNS(null, 'fill-rule', 'evenodd');
				path.setAttributeNS(null, 'd', roomPath.join(''));
				var pathA = svgEl('a');
				pathA.setAttributeNS('http://www.w3.org/1999/xlink', 'href', '#');
				pathA.appendChild(path);
				pathA.addEventListener('click', function(e) {
					e.preventDefault();
					a.click();
				}, false);
				g.appendChild(pathA);

				var gTitle = svgEl('title');
				gTitle.textContent = a.textContent;
				g.appendChild(gTitle);

				g.appendChild(layoutG);

				updateEditorBounds();
			}

			g.addEventListener('doUpdate', function() {
				updateRoom();
			}, false);

			updateRoom();

			return a;
		}

		function addFurniture(svgG, f, r, file, placeholders, list) {
			if (placeholders && Object.prototype.hasOwnProperty.call(f, 'placeholder') && !isNaN(f.placeholder)) {
				debugger;
			}

			if (isNaN(f.x)) {
				f.x = 0;
			}
			if (isNaN(f.y)) {
				f.y = 0;
			}
			if (isNaN(f.z)) {
				f.z = 0;
			}

			var li = document.createElement('li');
			list.appendChild(li);

			var g = svgEl('g');
			svgG.appendChild(g);

			var a = document.createElement('a');
			var lt = enums.layout_type.find(function(lt) {
				return lt.e === f.type;
			}) || enums.layout_type[0];
			a.textContent = lt.n;
			a.href = '#';
			a.addEventListener('click', function(e) {
				e.preventDefault();
				[].forEach.call(svgG.querySelectorAll('.active'), function(el) {
					el.classList.remove('active');
				});
				g.classList.add('active');
				selectedLayout = f;
				updateSelected();
			}, false);
			li.appendChild(a);

			var pathA = svgEl('a');
			pathA.setAttributeNS('http://www.w3.org/1999/xlink', 'href', '#');
			pathA.addEventListener('click', function(e) {
				e.preventDefault();
				a.click();
			}, false);
			g.appendChild(pathA);

			var path = svgEl('path');
			path.setAttributeNS(null, 'd', [
				'M',
				r.min[0] + f.x + 0.2,
				' ',
				r.min[1] + f.y + 0.2,
				'h.6v.6h-.6v-.6'
			].join(''));
			pathA.appendChild(path);

			var title = svgEl('title');
			title.textContent = a.textContent;
			g.appendChild(title);

		}

		var roomAddInst = document.createElement('label');
		roomAddInst.setAttribute('for', 'room-add-inst');
		roomAddInst.textContent = 'Instance';
		containerSide.appendChild(roomAddInst);

		var roomListInst = document.createElement('ul');
		containerSide.appendChild(roomListInst);

		var add = document.createElement('button');
		add.classList.add('add');
		add.textContent = '+';
		add.id = 'room-add-inst';
		add.addEventListener('click', function() {
			var r = {};
			instance.r = instance.r || [];
			instance.r.push(r);
			addRoom(r, instance, null, roomListInst).click();
		}, false);
		roomAddInst.appendChild(add);

		if (Object.prototype.hasOwnProperty.call(instance, 'r') && Array.isArray(instance.r)) {
			instance.r.forEach(function(r) {
				addRoom(r, instance, null, roomListInst);
			});
		}

		var roomAddTmpl = document.createElement('label');
		roomAddTmpl.setAttribute('for', 'room-add-tmpl');
		roomAddTmpl.textContent = 'Template';
		containerSide.appendChild(roomAddTmpl);

		var roomListTmpl = document.createElement('ul');
		containerSide.appendChild(roomListTmpl);

		add = document.createElement('button');
		add.classList.add('add');
		add.textContent = '+';
		add.id = 'room-add-tmpl';
		add.addEventListener('click', function() {
			var r = {};
			template.r = template.r || [];
			template.r.push(r);
			addRoom(r, template, instance, roomListTmpl).click();
		}, false);
		roomAddTmpl.appendChild(add);

		if (Object.prototype.hasOwnProperty.call(template, 'r') && Array.isArray(template.r)) {
			template.r.forEach(function(r) {
				addRoom(r, template, instance, roomListTmpl);
			})
		}

		addLayout.setAttribute('for', 'room-add-layout');
		addLayout.textContent = 'Layout';
		containerSide.appendChild(addLayout);

		add = document.createElement('button');
		add.classList.add('add');
		add.textContent = '+';
		add.id = 'room-add-layout';
		add.addEventListener('click', function() {
			var f = {};
			selectedFile.f = selectedFile.f || [];
			selectedRoom.layout = selectedRoom.layout || [];
			selectedRoom.layout.push(selectedFile.f.length);
			selectedFile.f.push(f);

			addFurniture(svg.querySelector('.room > .active > .layout'), f, selectedRoom, selectedFile, selectedPlaceholders, selectedLayoutList);

			selectedLayout = f;
			updateSelected();
		}, false);
		addLayout.appendChild(add);
		var addLayoutButton = add;

		updateSelected();

		function updateSelected() {
			if (selectedRoom) {
				addLayout.style.display = '';
			} else {
				addLayoutButton.style.display = 'none';
				addLayoutButton.disabled = true;
				addLayout.style.display = 'none';
				if (selectedLayoutList) {
					containerSide.removeChild(selectedLayoutList);
					selectedLayoutList = null;
				}
			}

			itemEditorPanel.innerHTML = '';
			if (selectedLayout) {
				addLayoutButton.style.display = 'none';
				addLayoutButton.disabled = true;
				buildLayoutEditor();
			} else if (selectedRoom) {
				addLayoutButton.style.display = '';
				addLayoutButton.disabled = false;
				buildRoomEditor();
			}
		}

		function doUpdate() {
			svg.querySelector('.room > .active').dispatchEvent(new Event('doUpdate'));
		}

		function TODO(obj, displayName, fieldName) {
			var field = document.createElement('div');
			field.classList.add('field');
			itemEditorPanel.appendChild(field);

			var label = document.createElement('label');
			label.textContent = displayName;
			field.appendChild(label);

			if (Object.prototype.hasOwnProperty.call(obj, fieldName)) {
				var input = document.createElement('input');
				input.type = 'text';
				input.value = JSON.stringify(obj[fieldName]);
				input.disabled = true;
				field.appendChild(input);

				var p = document.createElement('p');
				p.textContent = 'This field is not yet supported by the editor, but it is set in the JSON file. The value in the JSON file is displayed above and will not be modified.';
				field.appendChild(p);
			} else {
				var i = document.createElement('i');
				i.textContent = '(not yet implemented)';
				field.appendChild(i);
			}
		}

		function buildLayoutEditor() {
			var typeField = document.createElement('div');
			typeField.classList.add('field');
			itemEditorPanel.appendChild(typeField);

			var typeLabel = document.createElement('label');
			typeLabel.setAttribute('for', 'edit-layout-type');
			typeLabel.textContent = 'Type';
			typeField.appendChild(typeLabel);

			var prevType = selectedLayout.type;
			var typeSelect = document.createElement('select');
			typeSelect.id = 'edit-layout-type';
			var lastCategory = undefined;
			var group = typeSelect;
			enums.layout_type.forEach(function(e) {
				if (e.nc) {
					return;
				}
				if (e.c !== lastCategory) {
					if (e.hasOwnProperty('c')) {
						group = document.createElement('optgroup');
						group.label = e.c;
						typeSelect.appendChild(group);
					} else {
						group = typeSelect;
					}
					lastCategory = e.c;
				}
				var option = document.createElement('option');
				option.textContent = e.n;
				option.value = e.e;
				group.appendChild(option);
			});
			typeSelect.value = prevType;
			typeSelect.addEventListener('change', function() {
				markDirty();
				if (typeSelect.value === enums.layout_type[0].e) {
					delete selectedLayout.type;
				} else {
					selectedLayout.type = typeSelect.value;
				}
				doUpdate();
			}, false);
			typeField.appendChild(typeSelect);

			var constructionField = document.createElement('div');
			constructionField.classList.add('field');
			itemEditorPanel.appendChild(constructionField);

			var constructionLabel = document.createElement('label');
			constructionLabel.setAttribute('for', 'edit-layout-construction');
			constructionLabel.textContent = 'Construction';
			constructionField.appendChild(constructionLabel);

			var prevConstruction = selectedLayout.construction || 'NONE';
			var constructionSelect = document.createElement('select');
			constructionSelect.id = 'edit-layout-construction';
			var lastCategory = undefined;
			var group = constructionSelect;
			enums.construction_type.forEach(function(e) {
				if (e.nc) {
					return;
				}
				if (e.c !== lastCategory) {
					if (e.hasOwnProperty('c')) {
						group = document.createElement('optgroup');
						group.label = e.c;
						constructionSelect.appendChild(group);
					} else {
						group = constructionSelect;
					}
					lastCategory = e.c;
				}
				var option = document.createElement('option');
				option.textContent = e.n;
				option.value = e.e;
				group.appendChild(option);
			});
			constructionSelect.value = prevConstruction;
			constructionSelect.addEventListener('change', function() {
				markDirty();
				selectedLayout.construction = constructionSelect.value;
				doUpdate();
			}, false);
			constructionField.appendChild(constructionSelect);

			var digField = document.createElement('div');
			digField.classList.add('field');
			itemEditorPanel.appendChild(digField);

			var digLabel = document.createElement('label');
			digLabel.setAttribute('for', 'edit-layout-dig');
			digLabel.textContent = 'Dig';
			digField.appendChild(digLabel);

			var prevDig = selectedLayout.dig || 'Default';
			var digSelect = document.createElement('select');
			digSelect.id = 'edit-layout-dig';
			var lastCategory = undefined;
			var group = digSelect;
			enums.tile_dig_designation.forEach(function(e) {
				if (e.nc) {
					return;
				}
				if (e.c !== lastCategory) {
					if (e.hasOwnProperty('c')) {
						group = document.createElement('optgroup');
						group.label = e.c;
						digSelect.appendChild(group);
					} else {
						group = digSelect;
					}
					lastCategory = e.c;
				}
				var option = document.createElement('option');
				option.textContent = e.n;
				option.value = e.e;
				group.appendChild(option);
			});
			digSelect.value = prevDig;
			digSelect.addEventListener('change', function() {
				markDirty();
				selectedLayout.dig = digSelect.value;
				doUpdate();
			}, false);
			digField.appendChild(digSelect);

			var posField = document.createElement('div');
			posField.classList.add('field');
			itemEditorPanel.appendChild(posField);

			var posLabel = document.createElement('label');
			posLabel.setAttribute('for', 'edit-layout-pos-x');
			posLabel.textContent = 'Position';
			posField.appendChild(posLabel);

			var posX = document.createElement('input');
			var posY = document.createElement('input');
			var posZ = document.createElement('input');

			posX.type = 'number';
			posY.type = 'number';
			posZ.type = 'number';

			posX.id = 'edit-layout-pos-x';
			posY.id = 'edit-layout-pos-y';
			posZ.id = 'edit-layout-pos-z';

			posX.classList.add('mini');
			posY.classList.add('mini');
			posZ.classList.add('mini');

			posX.value = selectedLayout.x;
			posY.value = selectedLayout.y;
			posZ.value = selectedLayout.z;

			posX.min = -1;
			posY.min = -1;
			posZ.min = -1;

			posX.max = selectedRoom.max[0] - selectedRoom.min[0] + 1;
			posY.max = selectedRoom.max[1] - selectedRoom.min[1] + 1;
			posZ.max = selectedRoom.max[2] - selectedRoom.min[2] + 1;

			posX.addEventListener('change', function() {
				markDirty();
				selectedLayout.x = Number(posX.value);
				doUpdate();
			}, false);
			posY.addEventListener('change', function() {
				markDirty();
				selectedLayout.y = Number(posY.value);
				doUpdate();
			}, false);
			posZ.addEventListener('change', function() {
				markDirty();
				selectedLayout.z = Number(posZ.value);
				doUpdate();
			}, false);

			posField.appendChild(document.createTextNode('('));
			posField.appendChild(posX);
			posField.appendChild(document.createTextNode(', '));
			posField.appendChild(posY);
			posField.appendChild(document.createTextNode(', '));
			posField.appendChild(posZ);
			posField.appendChild(document.createTextNode(')'));

			var commentField = document.createElement('div');
			commentField.classList.add('field');
			itemEditorPanel.appendChild(commentField);

			var commentLabel = document.createElement('label');
			commentLabel.setAttribute('for', 'edit-layout-comment');
			commentLabel.textContent = 'Comment';
			commentField.appendChild(commentLabel);

			var commentInput = document.createElement('input');
			commentInput.type = 'text';
			commentInput.id = 'edit-layout-comment';
			commentInput.value = concatVariableString(selectedLayout.comment || '');
			commentInput.addEventListener('change', function() {
				markDirty();
				if (commentInput.value === '') {
					delete selectedLayout.comment;
				} else {
					selectedLayout.comment = maybeSplitVariableString(commentInput.value);
				}
				doUpdate();
			}, false);
			commentField.appendChild(commentInput);

			// TODO
			TODO(selectedLayout, 'Target', 'target');
			TODO(selectedLayout, 'Maximum users', 'has_users');
			TODO(selectedLayout, 'Ignore', 'ignore');
			TODO(selectedLayout, 'Make room', 'makeroom');
			TODO(selectedLayout, 'Internal', 'internal');
		}

		function buildRoomEditor() {
			var sizeX = document.createElement('input');
			var sizeY = document.createElement('input');
			var sizeZ = document.createElement('input');

			var typeField = document.createElement('div');
			typeField.classList.add('field');
			itemEditorPanel.appendChild(typeField);

			var typeLabel = document.createElement('label');
			typeLabel.setAttribute('for', 'edit-room-type');
			typeLabel.textContent = 'Type';
			typeField.appendChild(typeLabel);

			var prevType = selectedRoom.type;
			var typeSelect = document.createElement('select');
			typeSelect.id = 'edit-room-type';
			var lastCategory = undefined;
			var group = typeSelect;
			enums.room_type.forEach(function(e) {
				if (e.nc) {
					return;
				}
				if (e.c !== lastCategory) {
					if (e.hasOwnProperty('c')) {
						group = document.createElement('optgroup');
						group.label = e.c;
						typeSelect.appendChild(group);
					} else {
						group = typeSelect;
					}
					lastCategory = e.c;
				}
				var option = document.createElement('option');
				option.textContent = e.n;
				option.value = e.e;
				group.appendChild(option);
			});
			typeSelect.value = prevType;
			typeSelect.addEventListener('change', function() {
				onTypeChanged();
			}, false);
			typeField.appendChild(typeSelect);

			var subtypeField = document.createElement('div');
			subtypeField.classList.add('field');
			subtypeField.style.display = 'none';
			itemEditorPanel.appendChild(subtypeField);

			var rawtypeField = document.createElement('div');
			rawtypeField.classList.add('field');
			rawtypeField.style.display = 'none';
			itemEditorPanel.appendChild(rawtypeField);

			var stockpileListField = document.createElement('div');
			stockpileListField.classList.add('field');
			stockpileListField.style.display = 'none';
			itemEditorPanel.appendChild(stockpileListField);

			var stockpileListLabel = document.createElement('label');
			stockpileListLabel.textContent = 'Forbidden items';
			stockpileListField.appendChild(stockpileListLabel);

			var subtypeLabel = document.createElement('label');
			subtypeLabel.setAttribute('for', 'edit-room-subtype');
			subtypeLabel.textContent = 'Subtype';
			subtypeField.appendChild(subtypeLabel);

			var prevSubtype = undefined;
			var subtypeSelect = document.createElement('select');
			subtypeSelect.id = 'edit-room-subtype';
			initSubtypeSelect();
			subtypeSelect.addEventListener('change', function() {
				onTypeChanged();
			}, false);
			subtypeField.appendChild(subtypeSelect);

			var rawtypeLabel = document.createElement('label');
			rawtypeLabel.setAttribute('for', 'edit-room-rawtype');
			rawtypeLabel.textContent = 'Custom type';
			rawtypeField.appendChild(rawtypeLabel);

			var rawtypeInput = document.createElement('input');
			rawtypeInput.type = 'text';
			rawtypeInput.id = 'edit-room-rawtype';
			rawtypeInput.value = concatVariableString(selectedRoom.raw_type || '');
			rawtypeInput.addEventListener('change', function() {
				markDirty();
				selectedRoom.raw_type = maybeSplitVariableString(rawtypeInput.value);
				doUpdate();
			}, false);
			rawtypeField.appendChild(rawtypeInput);

			var posField = document.createElement('div');
			posField.classList.add('field');
			itemEditorPanel.appendChild(posField);

			var posLabel = document.createElement('label');
			posLabel.setAttribute('for', 'edit-room-pos-x');
			posLabel.textContent = 'Position';
			posField.appendChild(posLabel);

			var posX = document.createElement('input');
			var posY = document.createElement('input');
			var posZ = document.createElement('input');

			posX.type = 'number';
			posY.type = 'number';
			posZ.type = 'number';

			posX.id = 'edit-room-pos-x';
			posY.id = 'edit-room-pos-y';
			posZ.id = 'edit-room-pos-z';

			posX.classList.add('mini');
			posY.classList.add('mini');
			posZ.classList.add('mini');

			posX.value = selectedRoom.min[0];
			posY.value = selectedRoom.min[1];
			posZ.value = selectedRoom.min[2];

			posX.addEventListener('change', function() {
				markDirty();
				selectedRoom.min[0] = Number(posX.value);
				selectedRoom.max[0] = selectedRoom.min[0] - 1 + Number(sizeX.value);
				doUpdate();
			}, false);
			posY.addEventListener('change', function() {
				markDirty();
				selectedRoom.min[1] = Number(posY.value);
				selectedRoom.max[1] = selectedRoom.min[1] - 1 + Number(sizeY.value);
				doUpdate();
			}, false);
			posZ.addEventListener('change', function() {
				markDirty();
				selectedRoom.min[2] = Number(posZ.value);
				selectedRoom.max[2] = selectedRoom.min[2] - 1 + Number(sizeZ.value);
				doUpdate();
			}, false);

			posField.appendChild(document.createTextNode('('));
			posField.appendChild(posX);
			posField.appendChild(document.createTextNode(', '));
			posField.appendChild(posY);
			posField.appendChild(document.createTextNode(', '));
			posField.appendChild(posZ);
			posField.appendChild(document.createTextNode(')'));

			var sizeField = document.createElement('div');
			sizeField.classList.add('field');
			itemEditorPanel.appendChild(sizeField);

			var sizeLabel = document.createElement('label');
			sizeLabel.setAttribute('for', 'edit-room-size-x');
			sizeLabel.textContent = 'Size';
			sizeField.appendChild(sizeLabel);

			sizeX.type = 'number';
			sizeY.type = 'number';
			sizeZ.type = 'number';

			sizeX.id = 'edit-room-size-x';
			sizeY.id = 'edit-room-size-y';
			sizeZ.id = 'edit-room-size-z';

			sizeX.classList.add('mini');
			sizeY.classList.add('mini');
			sizeZ.classList.add('mini');

			sizeX.value = selectedRoom.max[0] - selectedRoom.min[0] + 1;
			sizeY.value = selectedRoom.max[1] - selectedRoom.min[1] + 1;
			sizeZ.value = selectedRoom.max[2] - selectedRoom.min[2] + 1;

			sizeX.min = 1;
			sizeY.min = 1;
			sizeZ.min = 1;

			sizeField.appendChild(document.createTextNode('('));
			sizeField.appendChild(sizeX);
			sizeField.appendChild(document.createTextNode(', '));
			sizeField.appendChild(sizeY);
			sizeField.appendChild(document.createTextNode(', '));
			sizeField.appendChild(sizeZ);
			sizeField.appendChild(document.createTextNode(')'));

			sizeX.addEventListener('change', function() {
				markDirty();
				selectedRoom.max[0] = selectedRoom.min[0] - 1 + Number(sizeX.value);
				doUpdate();
			}, false);
			sizeY.addEventListener('change', function() {
				markDirty();
				selectedRoom.max[1] = selectedRoom.min[1] - 1 + Number(sizeY.value);
				doUpdate();
			}, false);
			sizeZ.addEventListener('change', function() {
				markDirty();
				selectedRoom.max[2] = selectedRoom.min[2] - 1 + Number(sizeZ.value);
				doUpdate();
			}, false);

			initSize();

			var commentField = document.createElement('div');
			commentField.classList.add('field');
			itemEditorPanel.appendChild(commentField);

			var commentLabel = document.createElement('label');
			commentLabel.setAttribute('for', 'edit-room-comment');
			commentLabel.textContent = 'Comment';
			commentField.appendChild(commentLabel);

			var commentInput = document.createElement('input');
			commentInput.type = 'text';
			commentInput.id = 'edit-room-comment';
			commentInput.value = concatVariableString(selectedRoom.comment || '');
			commentInput.addEventListener('change', function() {
				markDirty();
				if (commentInput.value === '') {
					delete selectedRoom.comment;
				} else {
					selectedRoom.comment = maybeSplitVariableString(commentInput.value);
				}
				doUpdate();
			}, false);
			commentField.appendChild(commentInput);

			// TODO
			TODO(selectedRoom, 'Access path', 'accesspath');
			TODO(selectedRoom, 'Level', 'level');
			TODO(selectedRoom, 'Noble suite', 'noblesuite');
			TODO(selectedRoom, 'Queue', 'queue');
			TODO(selectedRoom, 'Workshop', 'workshop');
			TODO(selectedRoom, 'Maximum users', 'has_users');
			TODO(selectedRoom, 'Temporary', 'temporary');
			TODO(selectedRoom, 'Outdoor', 'outdoor');
			TODO(selectedRoom, 'Single biome', 'single_biome');
			TODO(selectedRoom, 'Requires walls', 'require_walls');
			TODO(selectedRoom, 'Requires floor', 'require_floor');
			TODO(selectedRoom, 'Minimum grass tiles', 'require_grass');
			TODO(selectedRoom, 'Allow placement in corridors', 'in_corridor');
			TODO(selectedRoom, 'Exits', 'exits');

			function initSubtypeSelect() {
				stockpileListField.style.display = 'none';
				while (stockpileListField.children.length > 1) {
					stockpileListField.removeChild(stockpileListField.lastChild);
				}
				var rt = enums.room_type.find(function(rt) {
					return rt.e === typeSelect.value;
				});
				if (!rt) {
					typeSelect.value = enums.room_type[0].e;
					onTypeChanged();
					return;
				}
				if (rt && rt.ste) {
					prevSubtype = selectedRoom[rt.ste];
					subtypeField.style.display = '';
					subtypeSelect.innerHTML = '';
					var lastCategory = undefined;
					var group = subtypeSelect;
					enums[rt.ste].forEach(function(e) {
						if (e.nc) {
							return;
						}
						if (e.c !== lastCategory) {
							if (e.hasOwnProperty('c')) {
								group = document.createElement('optgroup');
								group.label = e.c;
								subtypeSelect.appendChild(group);
							} else {
								group = subtypeSelect;
							}
							lastCategory = e.c;
						}
						var option = document.createElement('option');
						option.textContent = e.n;
						option.value = e.e;
						group.appendChild(option);
					});
					subtypeSelect.value = prevSubtype;
					var st = enums[rt.ste].find(function(st) {
						return st.e === prevSubtype;
					});
					if (!st) {
						prevSubtype = undefined;
						subtypeSelect.value = enums[rt.ste][0].e;
						onTypeChanged();
						return;
					}
					if (rt.e === 'stockpile') {
						if (Object.prototype.hasOwnProperty.call(selectedRoom, 'stock_disable') && Array.isArray(selectedRoom.stock_disable)) {
							selectedRoom.stock_disable = selectedRoom.stock_disable.filter(function(forbid) {
								return enums.stockpile_list.some(function(spl) {
									return spl.st === st.e && spl.e === forbid;
								});
							});
							if (selectedRoom.stock_disable.length === 0) {
								delete selectedRoom.stock_disable;
							}
						}

						var first = true;
						enums.stockpile_list.forEach(function(spl) {
							if (spl.st === st.e) {
								if (first) {
									first = false;
									stockpileListField.style.display = '';
								} else {
									stockpileListField.appendChild(document.createElement('br'));
								}

								var checkbox = document.createElement('input');
								checkbox.type = 'checkbox';
								checkbox.id = 'edit-room-stock_disable-' + spl.e;
								checkbox.checked = Array.isArray(selectedRoom.stock_disable) && selectedRoom.stock_disable.indexOf(spl.e) !== -1;
								checkbox.addEventListener('change', function() {
									var i = Array.isArray(selectedRoom.stock_disable) ? selectedRoom.stock_disable.indexOf(spl.e) : -1;
									if (checkbox.checked) {
										if (i === -1) {
											markDirty();
											selectedRoom.stock_disable = selectedRoom.stock_disable || [];
											selectedRoom.stock_disable.push(spl.e);
											doUpdate();
										}
									} else if (i !== -1) {
										markDirty();
										selectedRoom.stock_disable.splice(i, 1);
										if (selectedRoom.stock_disable.length === 0) {
											delete selectedRoom.stock_disable;
										}
										doUpdate();
									}
								}, false);
								stockpileListField.appendChild(checkbox);

								var label = document.createElement('label');
								label.classList.add('inline');
								label.setAttribute('for', checkbox.id);
								label.textContent = ' ' + spl.n;
								if (spl.hasOwnProperty('c') && spl.c !== st.n) {
									label.textContent += ' (' + spl.c + ')';
								}
								stockpileListField.appendChild(label);
							}
						});

						for (var ss = 1; ss <= 2; ss++) {
							if (st.hasOwnProperty('ss' + ss)) {
								if (first) {
									first = false;
									stockpileListField.style.display = '';
								} else {
									stockpileListField.appendChild(document.createElement('br'));
								}

								var checkbox = document.createElement('input');
								checkbox.type = 'checkbox';
								checkbox.id = 'edit-room-stock_specific' + ss;
								checkbox.checked = Boolean(selectedRoom['stock_specific' + ss]);
								checkbox.addEventListener('change', function() {
									markDirty();
									if (checkbox.checked) {
										selectedRoom['stock_specific' + ss] = true;
									} else {
										delete selectedRoom['stock_specific' + ss];
									}
									doUpdate();
								}, false);
								stockpileListField.appendChild(checkbox);

								var label = document.createElement('label');
								label.classList.add('inline');
								label.setAttribute('for', checkbox.id);
								label.textContent = ' ' + st['ss' + ss];
								stockpileListField.appendChild(label);
							} else {
								delete selectedRoom['stock_specific' + ss];
							}
						}
					} else {
						delete selectedRoom.stock_disable;
						delete selectedRoom.stock_specific1;
						delete selectedRoom.stock_specific2;
					}
				} else {
					rawtypeField.style.display = 'none';
					delete selectedRoom.raw_type;
					delete selectedRoom.stock_disable;
					delete selectedRoom.stock_specific1;
					delete selectedRoom.stock_specific2;
				}
			}

			function initSize() {
				var rt = enums.room_type.find(function(rt) {
					return rt.e === typeSelect.value;
				});
				var st = rt && rt.ste && enums[rt.ste].find(function(st) {
					return st.e === subtypeSelect.value;
				});

				if (st && (st.fw || st.fh)) {
					sizeX.disabled = true;
					sizeY.disabled = true;
					sizeZ.disabled = true;
					sizeX.value = st.fw;
					sizeY.value = st.fh;
					sizeZ.value = 1;
					selectedRoom.max[0] = selectedRoom.min[0] + st.fw - 1;
					selectedRoom.max[1] = selectedRoom.min[1] + st.fh - 1;
					selectedRoom.max[2] = selectedRoom.min[2];
				} else if (rt && (rt.fw || rt.fh)) {
					sizeX.disabled = true;
					sizeY.disabled = true;
					sizeZ.disabled = true;
					sizeX.value = rt.fw;
					sizeY.value = rt.fh;
					sizeZ.value = 1;
					selectedRoom.max[0] = selectedRoom.min[0] + rt.fw - 1;
					selectedRoom.max[1] = selectedRoom.min[1] + rt.fh - 1;
					selectedRoom.max[2] = selectedRoom.min[2];
				} else if (typeSelect.value === 'corridor') {
					sizeX.disabled = false;
					sizeY.disabled = false;
					sizeZ.disabled = false;
				} else {
					sizeX.disabled = false;
					sizeY.disabled = false;
					sizeZ.disabled = true;
					sizeZ.value = 1;
					selectedRoom.max[2] = selectedRoom.min[2];
				}
			}

			function onTypeChanged() {
				markDirty();
				if (typeSelect.value !== prevType) {
					var rt = enums.room_type.find(function(rt) {
						return rt.e === prevType;
					});
					if (rt && rt.ste) {
						delete selectedRoom[rt.ste];
						prevSubtype = undefined;
						subtypeField.style.display = 'none';
						subtypeSelect.innerHTML = '';
					}
					selectedRoom.type = typeSelect.value;
					prevType = typeSelect.value;
					initSubtypeSelect();
				}
				if (subtypeSelect.value !== prevSubtype) {
					var rt = enums.room_type.find(function(rt) {
						return rt.e === typeSelect.value;
					});
					selectedRoom[rt.ste] = subtypeSelect.value;
					prevSubtype = subtypeSelect.value;

					var st = rt && rt.ste && enums[rt.ste].find(function(st) {
						return st.e === subtypeSelect.value;
					});
					if (st.raw) {
						rawtypeField.style.display = '';
						selectedRoom.raw_type = rawtypeInput.value;
					} else {
						rawtypeField.style.display = 'none';
						delete selectedRoom.raw_type;
					}
				}
				initSize();
				doUpdate();
			}
		}
	};
})();
