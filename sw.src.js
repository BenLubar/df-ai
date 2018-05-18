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
workbox.precaching.precacheAndRoute([]);
