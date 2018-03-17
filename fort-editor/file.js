(function() {
	'use strict';

	var fileUpload = document.createElement('input');
	fileUpload.style.display = 'none';
	fileUpload.type = 'file';
	fileUpload.accept = 'application/json,.dat';
	fileUpload.addEventListener('change', function(e) {
		document.getElementById('loading').style.display = 'block';
		gtag('event', 'dfai_load_fort', {});
		var file = e.target.files[0];
		var reader = new FileReader();
		reader.addEventListener('loadend', function() {
			try {
				data = JSON.parse(reader.result);
			} catch (ex) {
				debugger;
			}
			resetUI();
		});
		reader.readAsText(file);
	});
	document.body.appendChild(fileUpload);

	window.doLoad = function doLoad() {
		if (dirty && !confirm('You have not saved your changes. Do you still want to load a different fortress layout?')) {
			return;
		}

		fileUpload.value = '';
		fileUpload.click();
	};

	var layerUpload = document.createElement('input');
	layerUpload.style.display = 'none';
	layerUpload.type = 'file';
	layerUpload.multiple = true;
	layerUpload.accept = '.bmp,image/*';
	layerUpload.addEventListener('change', function(e) {
		document.getElementById('loading').style.display = 'block';
		gtag('event', 'dfai_load_layers', {});

		layers.forEach(function(img) {
			if (img) {
				window.URL.revokeObjectURL(img.src);
			}
		});
		layers = [];
		var pattern = /^(.*-)[0-9]+(-.*-[0-9]+-[0-9]+\.[a-z]+)$/.exec(layerUpload.files[0].name);
		if (!pattern) {
			debugger; // TODO
			return;
		}

		var total = layerUpload.files.length;
		var loading = total;
		var queue = [];
		var workers = [];
		var possibilities = [];
		for (var i = 0; i < (navigator.hardwareConcurrency || 1) && i < loading; i++) {
			(function(w) {
				w.onmessage = function(e) {
					function merge(dst, src, equal, combine) {
						src.forEach(function(s) {
							if (!dst.some(function(d) {
								if (equal(d, s)) {
									combine(d, s);
									return true;
								}
								return false;
							})) {
								dst.push(s);
							}
						});
					}
					merge(possibilities, e.data.p, function(a, b) {
						return a.embarkWidth === b.embarkWidth &&
							a.embarkHeight === b.embarkHeight &&
							a.tileWidth === b.tileWidth &&
							a.tileHeight === b.tileHeight;
					}, function(a, b) {
						merge(a.tiles, b.tiles, function(a, b) {
							return a.hash === b.hash;
						}, function(a, b) {
							a.count += b.count;
						});
					});

					loading--;
					console.log('Processing images: %d of %d remaining', loading, total);
					if (queue.length) {
						w.postMessage(queue.shift());
					} else {
						workers.push(w);
					}

					if (loading === 0) {
						workers.forEach(function(w) {
							w.postMessage({d: null});
						});

						possibilities.forEach(function(p) {
							p.tiles.sort(function(a, b) {
								return b.count - a.count;
							});
						});
						possibilities.sort(function(a, b) {
							for (var i = 0; i < a.tiles.length && i < b.tiles.length; i++) {
								var diff = b.tiles[i].count - a.tiles[i].count;
								if (diff) {
									return diff;
								}
							}
							return a.tiles.length - b.tiles.length;
						});
						var colors = ['', '#000', '#fff', '#f00', '#ff0', '#0f0', '#0ff', '#00f', '#f0f'];
						debugger;
						possibilities.forEach(function(p) {
							var canvas = document.createElement('canvas');
							canvas.width = p.embarkWidth * p.tileWidth * 48;
							canvas.height = p.embarkHeight * p.tileHeight * 48;
							var ctx = canvas.getContext('2d');
							for (var y = 0; y < canvas.height; y++) {
								for (var x = 0; x < canvas.width; x++) {
									var c = p.full[x + canvas.width * y];
									while (colors.length <= c) {
										colors.push('#xxx'.replace(/x/g, function() { return Math.floor(Math.random() * 16).toString(16); }));
									}
									ctx.fillStyle = colors[c];
									ctx.fillRect(x, y, 1, 1);
								}
							}
							document.body.appendChild(canvas);
							document.body.appendChild(document.createElement('br'));
						});
						document.body.style.display = 'block';
						document.body.style.overflow = 'auto';
						debugger; // TODO
					}
				};
				workers.push(w);
			})(new Worker('tile-size-guesser.js'));
		}

		console.log('Processing images: %d of %d remaining', loading, total);
		[].forEach.call(layerUpload.files, function(file) {
			if (file.name.substring(0, pattern[1].length) !== pattern[1] || file.name.substring(file.name.length - pattern[2].length) !== pattern[2]) {
				debugger; // TODO
				return;
			}

			var idx = file.name.substring(pattern[1].length, file.name.length - pattern[2].length);

			while (layers.length <= idx) {
				layers.push(null);
			}
			layers[idx] = new Image();
			layers[idx].onload = function() {
				var canvas = document.createElement('canvas');
				canvas.width = this.width;
				canvas.height = this.height;
				var ctx = canvas.getContext('2d');
				ctx.drawImage(this, 0, 0);
				var payload = {
					o: idx,
					w: this.width / 48,
					h: this.height / 48,
					d: ctx.getImageData(0, 0, this.width, this.height).data
				};
				if (workers.length) {
					workers.shift().postMessage(payload);
				} else {
					queue.push(payload);
				}
			};
			layers[idx].src = window.URL.createObjectURL(file);
		});
	});
	document.body.appendChild(layerUpload);

	window.doLoadLayers = function doLoadLayers() {
		layerUpload.value = '';
		layerUpload.click();
	};

	window.doSave = function doSave() {
		gtag('event', 'dfai_save_fort', {});

		debugger; // TODO

		var url = window.URL.createObjectURL(blob);
		var a = document.createElement('a');
		a.style.display = 'none';
		document.body.appendChild(a);
		a.href = url;
		a.download = 'df-ai-plan.dat';
		a.click();
		window.URL.revokeObjectURL(url);
		document.body.removeChild(a);
		dirty = false;
	};
})();
