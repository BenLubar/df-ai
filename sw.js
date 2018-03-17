var realDBOpen = indexedDB.open;
indexedDB.open = function(name) {
	if (name === 'workbox-precaching') {
		return realDBOpen.call(indexedDB, 'workbox-precaching-df-ai');
	}
	return realDBOpen.apply(indexedDB, arguments);
};

importScripts('https://storage.googleapis.com/workbox-cdn/releases/3.0.0-beta.0/workbox-sw.js');

workbox.skipWaiting();
workbox.clientsClaim();
workbox.googleAnalytics.initialize();
workbox.routing.registerRoute(
	/^https:\/\/cors-anywhere\.herokuapp\.com\/(.*)$/,
	workbox.strategies.networkFirst({
		cacheName: 'cors-cache',
		plugins: [
			new workbox.expiration.Plugin({
				maxEntries: 50
			})
		]
	})
);
workbox.precaching.precacheAndRoute([
  {
    "url": "shared.css",
    "revision": "1fb2fd4aed4000b69d52cbe748918cd4"
  },
  {
    "url": "shared.js",
    "revision": "eee890cef2bd6cd48f53a0ba797f0955"
  },
  {
    "url": "fort-editor/file.js",
    "revision": "348e1e281bfa4d5872b059574df81c19"
  },
  {
    "url": "fort-editor/index.html",
    "revision": "68a0099f9d6982961773ec7eed442f10"
  },
  {
    "url": "fort-editor/style.css",
    "revision": "e2688a6fa42a20808ff407d311d8e867"
  },
  {
    "url": "fort-editor/tile-size-guesser.js",
    "revision": "b3723019d3e545234165e3b4dc0ca40c"
  },
  {
    "url": "fort-editor/ui.js",
    "revision": "880a130cf4a55fce4cecd22889c4e3d3"
  },
  {
    "url": "plan-editor/enums.js",
    "revision": "c7d9cf9705034b0cf06b6100c5e5461e"
  },
  {
    "url": "plan-editor/file.js",
    "revision": "8ce5845a95046554a124455361254885"
  },
  {
    "url": "plan-editor/index.html",
    "revision": "3f5e58979240b1ba26d923cd08e05b9f"
  },
  {
    "url": "plan-editor/plan-editor.js",
    "revision": "8f74d1060d27d02f54537731cf973dc8"
  },
  {
    "url": "plan-editor/room-editor.js",
    "revision": "f00c5a3f7ba7e702ee8d60564d0b2f07"
  },
  {
    "url": "plan-editor/style.css",
    "revision": "1f4078603d7dc958f348078f5a2d7a34"
  },
  {
    "url": "plan-editor/ui.js",
    "revision": "53f2603961d42f04c9000068c929fd2b"
  },
  {
    "url": "plan-editor/zipjs/WebContent/deflate.js",
    "revision": "d4e3a2a82db29526b20ce9314ce835fe"
  },
  {
    "url": "plan-editor/zipjs/WebContent/inflate.js",
    "revision": "fd952b261e960d0d7c8498569bee75d6"
  },
  {
    "url": "plan-editor/zipjs/WebContent/mime-types.js",
    "revision": "0b905934d7271bd0d03624ad556df7a0"
  },
  {
    "url": "plan-editor/zipjs/WebContent/z-worker.js",
    "revision": "7d40d9ac5e628c8f4ab5e683b11e09ba"
  },
  {
    "url": "plan-editor/zipjs/WebContent/zip-ext.js",
    "revision": "d4d95c839ec09ba4af58dc77e041bd2b"
  },
  {
    "url": "plan-editor/zipjs/WebContent/zip-fs.js",
    "revision": "cc38e0962c1e03da1c027183f931e691"
  },
  {
    "url": "plan-editor/zipjs/WebContent/zip.js",
    "revision": "3628e1a12984058c0b257af371629ded"
  },
  {
    "url": "/df-ai/plan-editor/",
    "revision": "301c795555ca4476b68268c3d6c77a9b"
  }
]);
