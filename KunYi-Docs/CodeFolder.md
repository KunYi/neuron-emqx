目前目錄結構如下
```
.
├── .github
├── cmake
├── CMakeLists.txt
├── create_cov_report.sh
├── default_plugins.json
├── deploy
├── dev.conf
├── docs
├── ft
├── include
├── Install-dependencies.md
├── LICENSE
├── neuron.conf
├── neuron.json
├── neuron.key
├── neuron.pem
├── package-sdk.sh
├── persistence
├── plugins
├── README-CN.md
├── README.md
├── sdk-install.sh
├── sdk-zlog.conf
├── simulator
├── src
├── tests
├── version
├── version.h.in
└── zlog.conf
```

* .github - 就是github actions 使用
* cmake  - 是編譯相關的設定
* deploy - 目前看起來是透過雲端部署用來偵測服務用的
* docs  - 當下有一些介紹架構的圖
* ft  - 應該是 functions test 看相關文件說明是使用 python 的 robotframework 架構進行測試，透過 restful api 進行
* include - 就程式本身架構的C/C++ header, 目前大概掃一下程式碼基本上C99標準才對
* persistence - 則是用來初始化 SQLite3 database 的SQL　命令，執行應該是有順序的，順序基本上就是按照檔案名稱前四個數字
* plugins - 各種plugins 它的設計是除了 event bus 外其他都是plugin，現在的版本v2.6 連本身的 webserver 也是plugin，可以看該目錄下的restful 目錄，基本上前後端靠這個部份 Restful API 相連
  而讀取裝置，開源的版本只有modbus 與 mqtt
* simulator - 開源的版本也只有 modbus，用來模擬給robotframework 測試用
* src - 這就是整個架構的主要程式碼，程式由 src/main.c 進入，在這個 src 下面又分了很多組件，在src下面這層就是簡單的執行初始化，判斷進入的狀態設定而已
* tests - 單元測試用的程式碼
