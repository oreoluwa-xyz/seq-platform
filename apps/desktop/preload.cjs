const {contextBridge, ipcRenderer} = require('electron');
contextBridge.exposeInMainWorld('seqDesktop', {
    getState: () => ipcRenderer.invoke('seq:window-state'),
    setPinned: value => ipcRenderer.invoke('seq:pin', value),
    setCompact: value => ipcRenderer.invoke('seq:compact', value),
    onState: callback => {
        const listener = (_event, state) => callback(state);
        ipcRenderer.on('seq:window-changed', listener);
        return () => ipcRenderer.removeListener('seq:window-changed', listener);
    }
});
