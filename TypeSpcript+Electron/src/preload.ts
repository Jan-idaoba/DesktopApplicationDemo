// See the Electron documentation for details on how to use preload scripts:
// https://www.electronjs.org/docs/latest/tutorial/process-model#preload-scripts

import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('electronAPI', {
  // 暴露一个函数 sendToService，接收一个 data 对象
  sendToService: (data: any) => ipcRenderer.send('send-to-cpp-service', data)
});