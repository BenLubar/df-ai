importScripts('https://storage.googleapis.com/workbox-cdn/releases/3.2.0/workbox-sw.js');

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
workbox.routing.registerRoute(
	/^https:\/\/imgs\.xkcd\.com\/comics\/dwarf_fortress(_2x)?\.png$|^https:\/\/s3\.amazonaws\.com\/github\/ribbons\/forkme_right_gray_6d6d6d\.png$/,
	workbox.strategies.staleWhileRevalidate({
		plugins: [
			new workbox.cacheableResponse.Plugin({
				statuses: [0, 200]
			})
		]
	})
);
workbox.precaching.precacheAndRoute([
  {
    "url": "favicon.ico",
    "revision": "1e20bcca0c8065cc81ef491e0214c3b3"
  },
  {
    "url": "index.html",
    "revision": "fdb26b3dfa67195666dd1495d92aafb2"
  },
  {
    "url": "manifest.json",
    "revision": "edfacda418701e29ffb0021eed8224b2"
  },
  {
    "url": "shared.css",
    "revision": "510c7d1d1fd130c08f6bc00a1c536675"
  },
  {
    "url": "shared.js",
    "revision": "2e770500dffe8746868fba93dca4a4a0"
  },
  {
    "url": "style.css",
    "revision": "eecda50121bff1f4010bad0176e9ab7c"
  },
  {
    "url": "fort-editor/file.js",
    "revision": "348e1e281bfa4d5872b059574df81c19"
  },
  {
    "url": "fort-editor/index.html",
    "revision": "dcaaa269455e141b67b7c26bae642a11"
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
    "revision": "5738caf853b04603430bec5777ab2a64"
  },
  {
    "url": "plan-editor/file.js",
    "revision": "8ce5845a95046554a124455361254885"
  },
  {
    "url": "plan-editor/index.html",
    "revision": "4b19f56a6cceeb30093b9a6f68785016"
  },
  {
    "url": "plan-editor/plan-editor.js",
    "revision": "8f74d1060d27d02f54537731cf973dc8"
  },
  {
    "url": "plan-editor/room-editor.js",
    "revision": "00627f58c8f47c17b4a91bfe936e5bc2"
  },
  {
    "url": "plan-editor/style.css",
    "revision": "bbbc93becd84f440730b004e11d9d5ef"
  },
  {
    "url": "plan-editor/ui.js",
    "revision": "bfaddb3530ea6c463b97b04f278d6c32"
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
  }
]);
