# Changelog

## [1.6.2](https://github.com/h4570/openbeach/compare/v1.6.1...v1.6.2) (2026-11-03)


### Performance Improvements

* **importer:** choose the PNG row filter in one pass ([#37](https://github.com/h4570/openbeach/issues/37)) ([2866bc3](https://github.com/h4570/openbeach/commit/2866bc3af4f0a91ade31c31bbdfc6d5e7c30e17d))
* **importer:** encode sprite and map PNGs on a worker pool ([#38](https://github.com/h4570/openbeach/issues/38)) ([30b280b](https://github.com/h4570/openbeach/commit/30b280bc09512bab4d67015fd113258dcbc3d7d4))
* **importer:** skip the map conversion when its inputs have not changed ([#36](https://github.com/h4570/openbeach/issues/36)) ([d0e358d](https://github.com/h4570/openbeach/commit/d0e358dca3a3fe326db2d3384d3473dd1ff9c6fb))
* **site:** split the browser import across web workers ([#41](https://github.com/h4570/openbeach/issues/41)) ([3d9a99f](https://github.com/h4570/openbeach/commit/3d9a99f86a2580eb42c051e09a5ca9983bb4be65)), closes [#39](https://github.com/h4570/openbeach/issues/39)

## [1.6.1](https://github.com/h4570/openbeach/compare/v1.6.0...v1.6.1) (2026-11-03)


### Bug Fixes

* **site:** center the game vertically on a phone ([24431b4](https://github.com/h4570/openbeach/commit/24431b4bb0d9d1c372476e361930fb163d8d41cb))
* **site:** center the game vertically on a phone ([0b8d3c6](https://github.com/h4570/openbeach/commit/0b8d3c6c42e3e3704a97c9fa9f606d05d784cbca))

## [1.6.0](https://github.com/h4570/openbeach/compare/v1.5.0...v1.6.0) (2026-11-03)


### Features

* **site:** caption the phone screenshot "the game on a phone" ([50dbdc2](https://github.com/h4570/openbeach/commit/50dbdc2ecdda9a753875fe3a144b76d508ef2a3b))
* **site:** fade the phone out at the bottom and make it larger ([02641b7](https://github.com/h4570/openbeach/commit/02641b72c6ba41a0a11449c3d7b210f9911e17fc))
* **site:** move the phone screenshot next to the others ([36898c0](https://github.com/h4570/openbeach/commit/36898c0928c89448cf59744e283a2d9c839d8670))
* **site:** show the game on a phone and list phones and computers ([7055f9d](https://github.com/h4570/openbeach/commit/7055f9d5eec95b9f346eaa1ead142022eef4c472))
* **site:** show the game on a phone and list phones and computers ([#28](https://github.com/h4570/openbeach/issues/28)) ([d7f01a4](https://github.com/h4570/openbeach/commit/d7f01a4a7a8231e8a6daa85f67b14b93455ea798))


### Bug Fixes

* **site:** align the screenshot captions ([cd6f50f](https://github.com/h4570/openbeach/commit/cd6f50f1f80e605414ca92266ec7f429848198dc))

## [1.5.0](https://github.com/h4570/openbeach/compare/v1.4.0...v1.5.0) (2026-11-03)


### Features

* attach a windows x64 zip and player guides to each release ([676cef6](https://github.com/h4570/openbeach/commit/676cef6cfc62673d130ca69e7bbf40f7bdaf4f99))
* **game:** show the openbeach logo as the program icon ([03cce54](https://github.com/h4570/openbeach/commit/03cce54f8a54b07db524e21fb48c3b354bde24b8))


### Bug Fixes

* **site:** fullscreen the page, not the canvas ([#25](https://github.com/h4570/openbeach/issues/25)) ([59d3548](https://github.com/h4570/openbeach/commit/59d3548977a422216a88d580bca61732a4ade77b))
* **site:** reword the last faq question in all languages ([#23](https://github.com/h4570/openbeach/issues/23)) ([4a9502b](https://github.com/h4570/openbeach/commit/4a9502b17b5b536a910307314f0fa4939a1b74d2))

## [1.4.0](https://github.com/h4570/openbeach/compare/v1.3.0...v1.4.0) (2026-11-03)


### Features

* **platform:** add touch controls to the browser build ([#16](https://github.com/h4570/openbeach/issues/16)) ([63fd714](https://github.com/h4570/openbeach/commit/63fd714b8ea86349834de7a1b140b17686b2af2f))
* **site:** iso faq, android, simpler drop box and a stargazers list ([#19](https://github.com/h4570/openbeach/issues/19)) ([1330568](https://github.com/h4570/openbeach/commit/1330568004944c2517e0e5c6184886c4a2bedf95))


### Bug Fixes

* **site:** keep the canvas at game size, fix phone landscape ([#20](https://github.com/h4570/openbeach/issues/20)) ([72d5e11](https://github.com/h4570/openbeach/commit/72d5e1127dea79bdd2cb3de58c7c5585541dd7bf))
* **site:** open the github and discord links in a new tab ([#13](https://github.com/h4570/openbeach/issues/13)) ([7144419](https://github.com/h4570/openbeach/commit/7144419e5a3616e4c010420d0ec32715b6c38b37))
* **tools:** do not count the pull request number in the subject length ([#21](https://github.com/h4570/openbeach/issues/21)) ([fdc6535](https://github.com/h4570/openbeach/commit/fdc653573491ffa7f13ffe3d276de85b1e499212))


### Performance Improvements

* **importer:** build the published browser importer with -O3 ([#18](https://github.com/h4570/openbeach/issues/18)) ([8f68545](https://github.com/h4570/openbeach/commit/8f68545f3b78ecf0519e46e8893be38a5d899812))
* **importer:** speed up the browser import of maps and sprites ([#17](https://github.com/h4570/openbeach/issues/17)) ([d26196d](https://github.com/h4570/openbeach/commit/d26196d67a8dd0ae040c3253a85923dd288e0611))

## [1.3.0](https://github.com/h4570/openbeach/compare/v1.2.0...v1.3.0) (2026-11-02)


### Features

* **site:** answer the usual questions in a faq section ([#10](https://github.com/h4570/openbeach/issues/10)) ([e90afe9](https://github.com/h4570/openbeach/commit/e90afe9ca93da0c30899538620e7fb96a89b4e30))

## [1.2.0](https://github.com/h4570/openbeach/compare/v1.1.0...v1.2.0) (2026-11-02)


### Features

* **site:** link the Discord server from the page and the readme ([#9](https://github.com/h4570/openbeach/issues/9)) ([f5ac715](https://github.com/h4570/openbeach/commit/f5ac7150fa993dbd1e0038060570fdbd670d5cc4))
* **site:** say up front that it plays from your own disc ([#7](https://github.com/h4570/openbeach/issues/7)) ([9347583](https://github.com/h4570/openbeach/commit/93475835c412f13ad519d8eed64a61a35013af2e))

## [1.1.0](https://github.com/h4570/openbeach/compare/v1.0.0...v1.1.0) (2026-11-02)


### Features

* **site:** describe the page to search engines and shared links ([#4](https://github.com/h4570/openbeach/issues/4)) ([0191e8a](https://github.com/h4570/openbeach/commit/0191e8a68b10659b29df3a3c808664968d26cd84))


### Bug Fixes

* **importer:** import the french, german and late english discs ([#3](https://github.com/h4570/openbeach/issues/3)) ([fca5de0](https://github.com/h4570/openbeach/commit/fca5de09913cad90fdeb1e9a3fd98bb44ea26683))

## [1.0.0](https://github.com/h4570/openbeach/compare/v0.9.0...v1.0.0) (2026-11-02)


### Features

* publish OpenBeach ([741fd5c](https://github.com/h4570/openbeach/commit/741fd5cdc314f84ef8603f9bb7cfafdfbf0fe28c))
