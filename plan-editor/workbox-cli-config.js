module.exports = {
	"globDirectory": "./",
	"globPatterns": [
		"*.{js,html,css}",
		"zipjs/WebContent/**.js"
	],
	"swSrc": "sw.src.js",
	"swDest": "sw.js",
	"globIgnores": [
		"sw.js",
		"sw.src.js",
		"workbox-cli-config.js"
	],
	"templatedUrls": {
		"/df-ai/plan-editor/": ["index.html"]
	}
};
