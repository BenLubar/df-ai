module.exports = {
	globDirectory: './',
	globPatterns: [
		'shared.*',
		'*/*.{js,html,css}',
		'plan-editor/zipjs/WebContent/**.js'
	],
	globIgnores: [
		'plan-editor/sw.js'
	],
	swDest: 'sw.js',
	swSrc: 'sw.src.js',
	templatedUrls: {
		'/df-ai/plan-editor/': 'plan-editor/index.html'
	}
};
