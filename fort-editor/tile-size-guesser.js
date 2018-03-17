'use strict';

function makeHasher(stdlib, foreign, buffer) {
	'use asm';

	var data8 = new stdlib.Uint8Array(buffer);
	var data32 = new stdlib.Uint32Array(buffer);
	var imul = stdlib.Math.imul;

	function palette(width, height, totalWidth, totalHeight) {
		width = width | 0;
		height = height | 0;
		totalWidth = totalWidth | 0;
		totalHeight = totalHeight | 0;

		var p = 0;
		var c = 0;
		var i = 0;
		var x = 0;
		var y = 0;
		var ij0 = 0;
		var i0 = 0;
		var ix = 0;
		var iy = 0;
		var j = 0;
		var j0 = 0;
		var jx = 0;
		var jy = 0;
		var length = 0;

		length = imul(totalWidth, totalHeight) | 0;

		// assume premultiplied alpha, little endian, make opaque
		for (i = 0; (i | 0) < (length | 0); i = (i + 1) | 0) {
			data32[(i << 2) >> 2] = (data32[(i << 2) >> 2] | (255 << 24));
		}

		for (y = 0; (y | 0) < (totalHeight | 0); y = (y + height) | 0) {
			for (x = 0; (x | 0) < (totalWidth | 0); x = (x + width) | 0) {
				ij0 = ((imul(y, totalWidth) | 0) + x) | 0;

				i0 = ij0;
				c = 0;

				for (iy = 0; (iy | 0) < (height | 0); iy = (iy + 1) | 0) {
					i = i0;
					for (ix = 0; (ix | 0) < (width | 0); ix = (ix + 1) | 0) {
						p = data32[(i << 2) >> 2] | 0;
						if ((p >>> 24) == 255) {
							c = (c + 1) | 0;
							j0 = ij0 | 0;
							for (jy = 0; (jy | 0) < (height | 0); jy = (jy + 1) | 0) {
								j = j0;
								for (jx = 0; (jx | 0) < (width | 0); jx = (jx + 1) | 0) {
									if ((data32[(j << 2) >> 2] | 0) == (p | 0)) {
										data32[(j << 2) >> 2] = c;
									}

									j = (j + 1) | 0;
								}
								j0 = (j0 + totalWidth) | 0;
							}
						}
						i = (i + 1) | 0;
					}
					i0 = (i0 + totalWidth) | 0;
				}
			}
		}
	}

	function tile(x, y, width, height, stride) {
		x = x | 0;
		y = y | 0;
		width = width | 0;
		height = height | 0;
		stride = stride | 0;

		var hash = 2166136261;
		var i = 0;
		var i0 = 0;
		var xi = 0;
		var yi = 0;

		width = width << 2;

		i0 = imul(stride, height) | 0;
		i0 = ((i0 | 0) + (imul(x, width) | 0)) | 0;
		i0 = (i0 + 3) | 0;

		for (yi = 0; (yi | 0) < (height | 0); yi = (yi + 1) | 0) {
			i = i0;
			for (xi = 0; (xi | 0) < (width | 0); xi = (xi + 1) | 0) {
				hash = imul(hash, 16777619) | 0;
				hash = hash ^ data8[i >> 0];
				i = (i + 1) | 0;
			}
			i0 = (i0 + stride) | 0;
		}

		return hash | 0;
	}

	return {
		palette: palette,
		tile: tile
	};
}

onmessage = function(e) {
	var opaqueID = e.data.o;
	var unknownWidth = e.data.w;
	var unknownHeight = e.data.h;
	var data = e.data.d;

	if (data === null) {
		close();
		return;
	}

	var dataCopy = new Uint8Array(data.length);
	var data32View = new Uint32Array(dataCopy.buffer);
	var hasher = makeHasher({
		Uint8Array: Uint8Array,
		Uint32Array: Uint32Array,
		Math: Math
	}, {}, dataCopy.buffer);

	var possibilities = [];
	for (var x = 1; x <= 16; x++) {
		for (var y = 1; y <= 16; y++) {
			if (unknownWidth % x === 0 && unknownWidth / x >= 4 && unknownHeight % y === 0 && unknownHeight / x >= 4) {
				possibilities.push({
					embarkWidth: x,
					embarkHeight: y,
					tileWidth: unknownWidth / x,
					tileHeight: unknownHeight / y,
					tiles: []
				});
			}
		}
	}

	var stride = unknownWidth * 48;

	possibilities.forEach(function(p) {
		dataCopy.set(data, 0);
		hasher.palette(p.tileWidth, p.tileHeight, stride, unknownHeight * 48);

		p.full = new Uint32Array(data32View.length);
		p.full.set(data32View, 0);

		for (var x = 0; x < p.embarkWidth * 48; x++) {
			for (y = 0; y < p.embarkHeight * 48; y++) {
				var hash = hasher.tile(x, y, p.tileWidth, p.tileHeight, stride * 4);
				if (!p.tiles.some(function(t) {
					if (t.hash === hash) {
						t.count++;
						return true;
					}
					return false;
				})) {
					p.tiles.push({
						hash: hash,
						count: 1
					});
				}
			}
		}

		p.tiles.sort(function(a, b) {
			return a.hash - b.hash;
		});
	});

	postMessage({o: opaqueID, p: possibilities});
};
