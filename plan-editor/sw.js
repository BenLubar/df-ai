importScripts('https://unpkg.com/workbox-sw@2.1.1/build/importScripts/workbox-sw.prod.v2.1.1.js');
importScripts('https://unpkg.com/workbox-google-analytics@2.1.1/build/importScripts/workbox-google-analytics.prod.v2.1.1.js');

const workboxSW = new WorkboxSW({skipWaiting: true, clientsClaim: true});
workbox.googleAnalytics.initialize();
workboxSW.router.registerRoute('https://cors-anywhere.herokuapp.com/(.*)',
	workboxSW.strategies.networkFirst({
		cacheName: 'cors-cache',
		cacheExpiration: {
			maxEntries: 50
		}
	})
);
workboxSW.precache([
  {
    "url": "enums.js",
    "revision": "7abd0099fb045354bcc4b9b1f275fca6"
  },
  {
    "url": "file.js",
    "revision": "a57e41aa7389ecfcd0bd01e92bb177ad"
  },
  {
    "url": "index.html",
    "revision": "6d6aa66037f93df424db0d9d5062d18d"
  },
  {
    "url": "plan-editor.js",
    "revision": "b0dde019722ba8317ae36364c5a27826"
  },
  {
    "url": "room-editor.js",
    "revision": "0e96461d19cc821c7c63f47ea29f17c0"
  },
  {
    "url": "style.css",
    "revision": "07ffa4cd01f1121b2e9871678bdbf141"
  },
  {
    "url": "ui.js",
    "revision": "53f2603961d42f04c9000068c929fd2b"
  },
  {
    "url": "zipjs/WebContent/deflate.js",
    "revision": "d4e3a2a82db29526b20ce9314ce835fe"
  },
  {
    "url": "zipjs/WebContent/inflate.js",
    "revision": "fd952b261e960d0d7c8498569bee75d6"
  },
  {
    "url": "zipjs/WebContent/mime-types.js",
    "revision": "0b905934d7271bd0d03624ad556df7a0"
  },
  {
    "url": "zipjs/WebContent/z-worker.js",
    "revision": "7d40d9ac5e628c8f4ab5e683b11e09ba"
  },
  {
    "url": "zipjs/WebContent/zip-ext.js",
    "revision": "d4d95c839ec09ba4af58dc77e041bd2b"
  },
  {
    "url": "zipjs/WebContent/zip-fs.js",
    "revision": "cc38e0962c1e03da1c027183f931e691"
  },
  {
    "url": "zipjs/WebContent/zip.js",
    "revision": "3628e1a12984058c0b257af371629ded"
  },
  {
    "url": "/df-ai/plan-editor/",
    "revision": "197a6645da0338ffac57f3ec835eedae"
  }
]);
