module.exports = {
	globDirectory: './',
	globPatterns: [
		'*.{js,html,css,json,ico}',
		'*/*.{js,html,css}',
		'plan-editor/zipjs/WebContent/**.js'
	],
	globIgnores: [
		'sw.js',
		'sw.src.js',
		'workbox-config.js',
		'plan-editor/sw.js'
	],
	swDest: 'sw.js',
	swSrc: 'sw.src.js'
};
