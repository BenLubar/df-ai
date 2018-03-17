self.registration.unregister().then(function() {
	new BroadcastChannel('precache-updates').postMessage('sw.js moved');
});
