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
workboxSW.precache([]);
