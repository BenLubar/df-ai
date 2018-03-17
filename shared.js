// service worker
if ('serviceWorker' in navigator) {
	if (navigator.serviceWorker.controller) {
		navigator.serviceWorker.addEventListener('controllerchange', function(e) {
			var ua = document.querySelector('#update-available');
			if (ua) {
				ua.style.display = 'block';
			} else {
				window.addEventListener('load', function() {
					document.querySelector('#update-available').style.display = 'block';
				}, false);
			}
			console.log('Update available:', e);
		});
	}

	navigator.serviceWorker.register('/df-ai/sw.js').then(function(registration) {
		console.log('Service Worker registration successful with scope: ', registration.scope);
	}).catch(function(err) {
		console.log('Service Worker registration failed: ', err);
	});
}

// analytics
window.dataLayer = window.dataLayer || [];
function gtag(){dataLayer.push(arguments);}
gtag('js', new Date());
gtag('config', 'UA-41367436-1');
