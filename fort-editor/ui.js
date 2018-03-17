(function() {
	window.addEventListener('beforeunload', function(e) {
		if (!dirty) {
			return;
		}

		var text = 'You have not saved your changes. Do you still want to leave?';
		e.returnValue = text;
		return text;
	}, false);

	var mainPanel = document.createElement('div');
	mainPanel.classList.add('main');
	document.body.appendChild(mainPanel);

	var sidePanel = document.createElement('div');
	sidePanel.classList.add('side');
	document.body.appendChild(sidePanel);

	var saveLoad = document.createElement('p');
	saveLoad.id = 'save-load';
	sidePanel.appendChild(saveLoad);

	var loadButton = document.createElement('button');
	loadButton.textContent = 'Load';
	loadButton.addEventListener('click', doLoad, false);
	saveLoad.appendChild(loadButton);

	saveLoad.appendChild(document.createTextNode(' '));

	var saveButton = document.createElement('button');
	saveButton.textContent = 'Save';
	saveButton.addEventListener('click', doSave, false);
	saveLoad.appendChild(saveButton);

	var secondSaveLoad = document.createElement('p');
	secondSaveLoad.classList.add('save-load');
	sidePanel.appendChild(secondSaveLoad);

	var layersButton = document.createElement('button');
	layersButton.textContent = 'Import Layer Images';
	layersButton.addEventListener('click', doLoadLayers, false);
	secondSaveLoad.appendChild(layersButton);

	document.getElementById('loading').style.display = 'none';
})();
