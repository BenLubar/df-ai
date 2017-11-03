(function() {
	var fileUpload = document.createElement('input');
	fileUpload.style.display = 'none';
	fileUpload.type = 'file';
	fileUpload.accept = 'application/zip';
	fileUpload.addEventListener('change', function(e) {
		document.getElementById('loading').style.display = 'block';
		lastModified = {};
		plans = {};
		rooms = {};
		var file = e.target.files[0];
		zip.createReader(new zip.BlobReader(file), function(reader) {
			reader.getEntries(function(entries) {
				var i = 0;
				function next() {
					var e = entries[i++];
					if (!e) {
						return reader.close(function() {
							console.log('closed zip reader');
							document.getElementById('loading').style.display = 'none';
							resetUI();
							dirty = false;
						});
					}
					if (e.directory) {
						return next();
					}
					var name = /^[^\/]+\/(plans|rooms\/(templates|instances)\/([^\/.]+))\/([^\/.]+)\.json$/.exec(e.filename);
					if (!name) {
						return next();
					}
					e.getData(new zip.TextWriter(), function(text) {
						var data;

						try {
							data = JSON.parse(text);
						} catch (ex) {
							console.error('error reading JSON file:', e.filename, ex);
							return next();
						}

						if (name[2]) {
							rooms[name[3]] = rooms[name[3]] || {instances: {}, templates: {}};
							rooms[name[3]][name[2]][name[4]] = data;
							lastModified['df-ai-blueprints/rooms/' + name[2] + '/' + name[3] + '/' + name[4] + '.json'] = e.lastModDate;
						} else {
							plans[name[4]] = data;
							lastModified['df-ai-blueprints/plans/' + name[4] + '.json'] = e.lastModDate;
						}
						next();
					});
				}
				console.log('processing ' + entries.length + ' zip file entries');
				next();
			});
		}, function(error) {
			console.error(error);
			document.getElementById('loading').style.display = 'none';
			alert('Error: ' + error);
		});
	}, false);
	document.body.appendChild(fileUpload);

	window.doLoad = function doLoad() {
		if (dirty && !confirm('You have not saved your changes. Do you still want to load a different set of blueprints?')) {
			return;
		}

		fileUpload.value = '';
		fileUpload.click();
	};

	function prettyJSON(data, indent) {
		indent = indent || '';
		if (Array.isArray(data)) {
			if (data.every(function(x) { return !isNaN(x); })) {
				return '[' + data.join(', ') + ']';
			}
			return '[\n\t' + indent + data.map(function(x) {
				return prettyJSON(x, indent + '\t');
			}).join(',\n\t' + indent) + '\n' + indent + ']';
		}
		if (data === null) {
			return 'null';
		}
		if (typeof data === 'object') {
			return '{\n\t' + indent + Object.keys(data).map(function(k) {
				var v = data[k];
				return JSON.stringify(k) + ': ' + prettyJSON(v, indent + '\t');
			}).join(',\n\t' + indent) + '\n' + indent + '}';
		}
		return JSON.stringify(data);
	}

	window.doSave = function doSave() {
		zip.createWriter(new zip.BlobWriter(), function(writer) {
			var directories = ['df-ai-blueprints/', 'df-ai-blueprints/plans/', 'df-ai-blueprints/rooms/', 'df-ai-blueprints/rooms/instances/', 'df-ai-blueprints/rooms/templates/'];
			var files = [].concat.apply(Object.keys(plans).map(function(p) {
				return {
					name: 'df-ai-blueprints/plans/' + p + '.json',
					data: plans[p]
				};
			}), Object.keys(rooms).map(function(f) {
				return Object.keys(rooms[f].instances).map(function(n) {
					return {
						name: 'df-ai-blueprints/rooms/instances/' + f + '/' + n + '.json',
						data: rooms[f].instances[n]
					};
				});
			}).concat(Object.keys(rooms).map(function(f) {
				return Object.keys(rooms[f].templates).map(function(n) {
					return {
						name: 'df-ai-blueprints/rooms/templates/' + f + '/' + n + '.json',
						data: rooms[f].templates[n]
					};
				});
			})));

			function next() {
				if (directories.length) {
					var d = directories.shift();
					writer.add(d, null, next, function() {}, {
						directory: true
					});
				} else if (files.length) {
					var f = files.shift();
					writer.add(f.name, new zip.TextReader(prettyJSON(f.data)), next, function() {}, {
						lastModDate: lastModified[f.name],
						level: 9
					});
				} else {
					writer.close(function(blob) {
						var url = window.URL.createObjectURL(blob);
						var a = document.createElement('a');
						a.style.display = 'none';
						document.body.appendChild(a);
						a.href = url;
						a.download = 'df-ai-blueprints-custom.zip';
						a.click();
						window.URL.revokeObjectURL(url);
						document.body.removeChild(a);
						dirty = false;
					});
				}
			}
		}, function(error) {
			console.error(error);
			alert('Error: ' + error);
		});
	};
})();
