# Сайт Moonbow

Новый сайт плеера (ноябрь 2026). Прошлый лежит в `../archive/site-2026-10/`. Замысел, концепция
«Восемь кадров» и вся система (цвета, шрифты, движение): [DESIGN.md](DESIGN.md).

## Посмотреть

- **Без сервера:** `npm run build`, потом двойной щелчок по `dist-local/index.html`. Это копия сайта с
  относительными ссылками, она работает прямо с диска.
- **С сервером:** `npm run build && npm run preview`, затем http://127.0.0.1:4471/.
- **Разработка:** `npm run dev` (Astro и пересборка клиентского скрипта на лету).

Для хостинга нужна папка `dist/`: это обычная статика, подойдёт любой хостинг (Netlify, Cloudflare
Pages, GitHub Pages, свой nginx). Страница 404 лежит в `dist/404.html`.

## Адреса

- Сайт публикуется на GitHub Pages репозитория: `https://piwqust.github.io/moonbow/` (workflow
  `.github/workflows/moonbow-site.yml`, деплоится `dist-local/`, поэтому работает и в подпапке). Свой домен:
  поменяйте `SITE_URL` в `astro.config.mjs` и `site.url` в `src/data/site.ts`.
- Кнопки «Скачать» ведут на `releases/latest/download/Moonbow-windows-x64-setup.exe` и `.zip`: имена файлов без
  версии (`tools/package-release.ps1`), поэтому ссылки сами переходят на новый релиз. Размеры файлов в
  `src/data/site.ts` (`download`) обновляйте вручную.

## Откуда берутся данные

- **Тексты:** `src/data/site.ts`.
- **Все настройки, команды с клавишами и форматы:** `src/data/app/*.json`, собираются из кода плеера:
  `npm run extract` (читает `src/core/SettingsSchema.cpp`, `src/app/Actions.cpp`, строки перевода и
  списки расширений). После изменений в плеере запустите и пересоберите сайт.
- **Замер запуска:** `src/data/measurements/startup-2026-11-20.txt`, сырые строки 20 запусков с файлом
  и 20 с медиатекой (`qa/perf/firstframe.ps1 -Runs 40`). Медианы перенесены в `trace` в `site.ts`.
- **Снимки экрана:** `src/assets/shots/`, кадры для «Восьми кадров»: `src/assets/frames/`. Всё снято с
  настоящего `build\Moonbow.exe` в скрытых тестовых окнах, ничего не появляется на экране:

  ```bash
  node tools/shots/make-media.mjs                             # демо-медиатека из фото (один раз)
  powershell -File tools/shots/capture.ps1 [-Only hero,series] # снимки экранов
  powershell -File tools/shots/startup.ps1                    # кадры запуска (в %TEMP%, выбрать вручную)
  node tools/shots/crops.mjs                                  # части окон для телефонов
  node tools/og.mjs                                           # картинка для соцсетей
  ```

  Кадры запуска: `window.png`, `player.png`, `fade1.jpg` и `full.jpg` сняты `startup.ps1`. Проявление
  картинки длится 200 мс, а снимок окна такого размера занимает ~200 мс, поэтому `fade0`, `fade2` и
  `fade3` смешаны из `player.png` и `full.jpg` по кривой самого плеера (ease-out, `PlayerView`); сайт
  говорит об этом под кадровым окном.

  Демо-медиа выдуманы: фильмы, сериалы «Дом у моря» и «Северный ветер», исполнители и альбомы. Кадры и
  обложки из фото Unsplash (авторы в `src/data/credits.json` и на странице «Скачать»), звук
  синтезирован. Для своих снимков положите настоящие кадры и обложки и запустите те же скрипты.

## Устройство

- Astro собирает статичный HTML. Весь клиентский код (`src/client`, GSAP) собирается esbuild в один
  обычный скрипт `public/assets/site.js` без модулей и роутера: поэтому сайт открывается с диска.
- `scripts/postbuild.mjs` вписывает в подвал настоящий вес кода каждой страницы и делает `dist-local/`.
- Плавной прокрутки и загрузчика нет намеренно (см. DESIGN.md).
