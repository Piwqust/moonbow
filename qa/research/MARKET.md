# Lumen: исследование рынка плееров (октябрь 2026)

Сравнение Lumen с VLC, MPC-HC/BE, mpv и его оболочками, PotPlayer, IINA, Infuse, Windows Media Player, Screenbox,
медиацентрами и музыкальными плеерами. Данные о Lumen взяты из кода (`SettingsSchema.cpp`, `Actions.cpp`,
`Player.cpp`, `SeekPreview.cpp`), README и STATUS (приложение А). Данные о конкурентах взяты из официальных
release notes, README на GitHub, новостей и форумов 2025–2026 годов (раздел 8). Сырые заметки лежат в приложении Б.

## 1. Краткий вывод

1. По ядру Lumen уже на уровне лидеров: mpv 0.41 тоже перешёл на `gpu-next`, по скорости появления окна (~80–110 мс) и простою (0% CPU в медиатеке) Lumen впереди почти всех.
2. Главное отличие Lumen — сочетание продуманного нативного UI, медиатеки и mpv-ядра на Windows. Такого нет ни у кого: MPC и mpv без медиатеки, у VLC 3 устаревший UI, WMP медленный и бедный, у Screenbox ядро libVLC/UWP.
3. Самый заметный пробел по функциям — субтитры: нет поиска онлайн (OpenSubtitles есть у MPC-HC, MPC-BE, uosc, IINA, SMPlayer, PotPlayer) и нет ИИ-субтитров (PotPlayer уже умеет через Whisper, VLC пока только показал демо).
4. Второй пробел — аудио для «аудиофилов»: нет passthrough (`audio-spdif`), exclusive WASAPI, gapless/crossfade-настроек, текстов песен и last.fm. Почти всё это делается одной-двумя опциями mpv.
5. Третий — качество видео: нельзя выбрать апскейлер, настроек tone-mapping мало, нет RTX/Intel Super Resolution (есть в VLC, MPCVR, PotPlayer).
6. Медиатека пока проще, чем в Infuse/Plex: нет сериалов, «Следующей серии», постеров, автопропуска заставок. При этом именно здесь мало кто из десктоп-плееров конкурирует.
7. Lumen сам ломает zero-copy аппаратного декодирования: кроп, резкость и явный метод деинтерлейса идут через lavfi (`BuildVf`). Это копирование кадров в RAM и нагрузка на CPU на 4K.
8. Превью на seekbar строится отдельным mpv с `hwdec=no` и `hr-seek=yes`. На 4K HEVC это дорого. Лидеры (thumbfast, MPC) держат постоянный процесс и ищут по ключевым кадрам.
9. Пользователи ненавидят рекламу и бандлы (KMPlayer, GOM, частично PotPlayer), навязанные редизайны (Plex 2025–2026, VLC 4), тормоза медиатеки (Apple Music, WMP) и «конфиг вместо GUI» (mpv). У Lumen ничего из этого нет, и это стоит сохранить.
10. План: P0 — субтитры онлайн, passthrough/exclusive/gapless, zero-copy фильтры, быстрые превью, оверлей статистики. P1 — апскейлеры и tone-mapping, сериалы и «Следующая серия», тексты песен, жесты, меню с поиском.

## 2. Таблица сравнения функций

Обозначения: ✓ есть; ч — частично или через плагин/внешний бинарь; ✗ нет; ? — не удалось подтвердить.
mpv(+uosc) = mpv 0.41 с uosc и thumbfast.

| Функция | **Lumen** | VLC 3.0.23 | MPC-HC 2.8 | MPC-BE 1.8 | mpv(+uosc) | PotPlayer | IINA 1.4 | Infuse 8.4 | WMP (Win11) | Screenbox | foobar2000 2.25 | MusicBee 3.6 | Spotify |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Превью кадров на seekbar | ✓ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ? | — | — | — |
| Главы на шкале | ✓ | ч | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ? | — | — | — |
| Продолжение с позиции | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ч | ? | ч | ч | ✓ |
| Вторичные субтитры | ✓ | ✗ | ч (SRT) | ? | ✓ | ✓ | ✓ | ? | ✗ | ✗ | — | — | — |
| Поиск субтитров онлайн | ✗ | ч (VLSub) | ✓ | ✓ | ✓ | ✓ | ✓ (плагин) | ✓ | ✗ | ✗ | — | — | — |
| ИИ-субтитры/перевод | ✗ | ✗ (демо CES 2025) | ✗ | ✗ | ✗ | ✓/ч (Whisper) | ✗ | ✗ | ✗ | ✗ | — | — | — |
| Стиль субтитров | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ (системный) | ч | — | — | — |
| HDR-вывод (passthrough) | ✓ | ✓ | ✓ (MPCVR) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ч | — | — | — |
| Настраиваемый HDR→SDR tone-mapping | ч | ч | ✓ | ✓ | ✓ | ✓ | ч | ✗ | ✗ | ✗ | — | — | — |
| Выбор апскейлера | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ч | ✗ | ✗ | ✗ | — | — | — |
| RTX/AMD/Intel Super Resolution | ✗ | ✓ | ✓ | ✓ | ч (vf вручную) | ✓ | ✗ | ✗ | ✗ | ✗ | — | — | — |
| Интерполяция кадров | ч | ✗ | ч (SVP) | ч (SVP) | ✓ (+SVP) | ✓/ч | ч | ✗ | ✗ | ✗ | — | — | — |
| Деинтерлейс | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |
| Аппаратное декодирование | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |
| Оверлей статистики | ✗ (скрыт) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ч | ✗ | ✗ | — | — | — |
| Passthrough AC3/DTS/TrueHD | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ? | ч | ✗ | — |
| WASAPI exclusive | ✗ | ч | ✓ | ✓ | ✓ | ✓ | — | — | ✗ | ✗ | ✓ | ✓ | ✗ |
| Gapless | ч (по умолч. mpv) | ✗ | ✗ | ✗ | ✓ | ч | ✓ | ✓ | ✓ | ? | ✓ | ✓ | ✓ |
| Crossfade | ✗ | ✗ | ✗ | ✗ | ✗ | ч | ✗ | ✗ | ✗ | ✗ | ч (компонент) | ✓ | ✓ |
| ReplayGain | ✓ (теги) | ✓ | ✓ | ✓ | ✓ | ч | ✓ | ✗ | ✗ | ✗ | ✓ (+сканер) | ✓ (+сканер) | ✓ (нормализация) |
| Нормализация громкости на лету | ✓ | ✓ | ✓ | ✓ | ✓ (af) | ✓ | ✓ | ✓ | ✗ | ✗ | ч | ✓ | ✓ |
| Эквалайзер | ✓ | ✓ | ✗ | ч | ч (af) | ✓ | ✓ | ✗ | ✓ | ✗ | ✓ | ✓ | ✓ |
| Тексты песен | ✗ | ✗ | ✗ | ✗ | ✗ | ч | ✗ | ✗ | ✗ | ✗ | ч | ✓ | ✓ (синхр.) |
| Скробблинг last.fm | ✗ (скрыт) | ✓ | ✗ | ✗ | ч (скрипт) | ✗ | ✗ | ✗ | ✗ | ✗ | ч | ✓ | ✓ |
| Медиатека | ✓ | ч | ✗ | ✗ | ✗ | ✗ | ч (история) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Постеры/метаданные (TMDB) | ч | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ч | ✗ | — | ✓ (обложки) | ✓ |
| Сериалы и «Следующая серия» | ✗ | ✗ | ч (следующий файл) | ч | ч (autoload) | ч | ч | ✓ | ✗ | ✗ | — | — | — |
| Пропуск заставок/титров | ✗ | ✗ | ✗ | ✗ | ч (скрипты) | ✓ (по главам/закладкам) | ✗ | ✓ | ✗ | ✗ | — | — | — |
| URL через yt-dlp | ч (выкл., без бинаря) | ч (Lua) | ✓ | ✓ (+свой парсер) | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ | — | — | — |
| DLNA/UPnP, SMB-браузер | ✗ | ✓ | ✗ | ✗ | ✗ | ✓ | ✗ | ✓ | ✗ | ✓ | ✓ (2.25) | ч | — |
| Chromecast/AirPlay | ✗ (скрыт) | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ (AirPlay) | ✓ | ✗ | ✓ | ✓ (UPnP) | ч | ✓ (Connect) |
| Веб/телефонный пульт | ✗ (скрыт) | ✓ | ✓ (2.8.1) | ✓ | ч (скрипт) | ✗ | ✗ | — | ✗ | ✗ | ч | ч | ✓ |
| Переназначение клавиш | ✓ | ✓ | ✓ | ✓ | ✓ (input.conf) | ✓ | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ | ✗ |
| Поиск по настройкам | ✓ | ✗ | ✗ | ✗ | ✗ (✓ в mpv.net) | ✗ | ✗ | — | — | — | ✗ | ✗ | ✓ |
| PiP / мини-плеер | ✓ | ч | ч | ч | ч | ✓ | ✓ | ✓ | ✓ | ✓ | ч | ✓ | ✓ |
| SMTC и медиаклавиши | ✓ | ч | ? | ? | ✓ (0.39+) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Скрипты и плагины | ✓ (mpv Lua/JS) | ✓ | ✗ | ✗ | ✓ | ч | ✓ (JS) | ✗ | ✗ | ✗ | ✓ | ✓ | ✗ |
| Без рекламы и бандлов | ✓ | ✓ | ✓ | ✓ | ✓ | ч | ✓ | ✓ (платный) | ✓ | ✓ | ✓ | ✓ | ✗ (free) |

Коротко о плеерах, которых нет в таблице:
- **mpv.net:** поиск по настройкам и клавишам, темы, .NET-расширения.
- **SMPlayer 25.6:** настройки на каждый файл, OpenSubtitles, YouTube, генератор листа миниатюр.
- **Celluloid 0.27:** плавающие контролы, экран загрузки, быстрый старт.
- **KMPlayer и GOM:** реклама, у GOM подтверждена история с малварью в 2019 году.
- **Dopamine 3.0:** gapless, crossfade, умные плейлисты, last.fm, тексты песен, Discord RPC.
- **Kodi 22 RC:** FFmpeg 9, HDR-оверлеи, tone-mapping PGS-субтитров на Windows.
- **Jellyfin Desktop 2.0:** Qt6 + libmpv, passthrough.
- **Stremio (сент. 2026):** превью времени на шкале, перенос языка аудио и субтитров между сериями, задержка субтитров шагом 100 мс с ускорением при удержании, циклическое переключение fit/fill/stretch по клавише.

## 3. Настройки: что есть у лидеров и чего нет у Lumen

Lumen уже глубже по настройкам, чем WMP, Screenbox, Infuse и Spotify. До уровня MPC-HC, PotPlayer и mpv не хватает
следующего. В скобках указаны опции mpv, через которые это делается.

**Видео**
- Апскейлер, даунскейлер, хрома-скейлер (`scale`, `dscale`, `cscale`) с пресетами «Быстро / Качество / Максимум»
  (mpv `--profile=fast|high-quality`, `ewa_lanczossharp`). Есть у MPC (MPCVR, madVR) и PotPlayer.
- Tone-mapping как постоянная настройка: алгоритм (`tone-mapping=auto|bt.2446a|spline|st2094-40`),
  `hdr-compute-peak`, `target-peak`, `tone-mapping-mode`, `gamut-mapping-mode`. Сейчас это есть только в быстрых настройках.
- RTX VSR и Intel/AMD Super Resolution (`vf=d3d11vpp=scale=2:scaling-mode=nvidia|intel`) и NVIDIA TrueHDR
  (`nvidia-true-hdr`). Работает только с `gpu-api=d3d11` и `hwdec=d3d11va`.
- ICC-профиль и автоопределение монитора (`icc-profile-auto`). В коде есть упоминания icc, в настройках пункта нет.
- Debanding как переключатель с пресетом (`deband=yes`, `deband-iterations`, `deband-threshold`). Сейчас есть только радиус.
- Пользовательские GLSL-шейдеры (`glsl-shaders`, например FSRCNNX, Anime4K, CAS) с выбором из папки. Это любимая
  функция mpv-сообщества и замена `sharpen` через lavfi.
- Синхронизация с дисплеем: `video-sync=display-resample` + `interpolation` + `tscale`. Простой переключатель
  «Плавность (интерполяция)» вместо раздела «Разработчик».
- Пресеты «на файл» (как в SMPlayer): запоминать аспект, задержки и дорожки для конкретного файла.

**Аудио**
- Passthrough (`audio-spdif=ac3,eac3,dts,dts-hd,truehd`) с чекбоксами форматов.
- Exclusive WASAPI (`audio-exclusive=yes`) и выбор размера буфера (`audio-buffer`).
- Gapless (`gapless-audio=yes|weak|no`) и `prefetch-playlist=yes`. Для музыки это обязательно.
- Crossfade. В mpv его нет, нужна своя реализация: две секунды `af=afade` плюс предзагрузка следующего трека или скрипт.
- `dynaudnorm` как альтернатива `loudnorm`. `loudnorm` в реальном времени «качает» громкость и даёт задержку.
- Автозагрузка внешних аудиодорожек (`audio-file-auto=fuzzy`). Об этом просят пользователи Screenbox.
- Громкость до 200%. VLC ценят именно за это (тред Reddit), у Lumen максимум 150%.

**Субтитры**
- Онлайн-поиск (OpenSubtitles REST, поиск по хэшу и имени), языки поиска, аккаунт.
- `sub-fix-timing`, `sub-ass-override` уже есть. Не хватает `sub-blur`, фона-подложки как основного стиля
  (`sub-border-style=background-box`, сейчас есть только в LiveState), `sub-margin-y`, «субтитры в чёрных полях»
  (`sub-use-margins`, `sub-ass-force-margins`), стиля по системным настройкам Windows (это сделал WMP в 2026).
- Позиция и стиль вторичных субтитров (`secondary-sub-pos`, сверху, как в MPC-HC 2.8.1).
- Автокопирование строки субтитров в буфер (MPC-HC 2.8.1) и клик по слову (PotPlayer) для изучающих языки.

**Интерфейс и поведение**
- Скрытые пункты, которые стоит реализовать, а не держать: «Режим разработчика» (статистика), компактный режим,
  длинный шаг перемотки, пауза при захвате аудио другой программой, сбор статистики.
- Поведение мыши, которое ценят в uosc: колесо над громкостью или скоростью меняет значение, ПКМ сбрасывает;
  колесо над seekbar перематывает.
- Удержание клавиши или ЛКМ для временной скорости 2x, как на YouTube. Screenbox сделал раскладку клавиш как на YouTube.
- Действие «Удалить файл и следующий» (в корзину, с подтверждением). Есть в uosc, очень любят для сериалов. Делать
  только мягкое удаление: в mpv-hero жаловались на жёсткое удаление по Delete.
- «Закреплённые» метки времени рядом со шкалой (таймер у seekbar, MPC-HC 2.7.4), оставшееся время или время окончания.
- Профили и пресеты вида (MPC-HC 2.7.4, «пресет 4»), импорт и экспорт настроек.

## 4. Дизайн и UX: лучшие паттерны и что взять

**Seekbar**
- *uosc:* таймлайн сворачивается в тонкую полоску 2–4 px, когда курсор далеко, и разворачивается при приближении.
  Главы показаны диапазонами, а не точками. **Взять:** режим «тонкий прогресс» в полноэкранном режиме вместо полного
  скрытия (ModernZ: `progress-toggle`).
- *ModernZ:* четыре стиля меток глав (gap, triangle, bar, single-bar), высота шкалы S–XL.
  **Взять:** разрыв (gap) в шкале на границе главы, он лучше читается в монохроме.
- *Stremio 2026, YouTube:* время над курсором даже без превью, мгновенно. **Взять:** показывать время сразу, а кадр
  подгружать по готовности. Скорее всего, Lumen так уже делает, стоит проверить на 4K.
- *MPC-HC 2.7.4:* таймер прямо у seekbar. **Взять:** клик по времени переключает «прошло / осталось / конец в 23:41».

**OSD и контролы**
- *uosc:* появление по близости курсора, а не по любому движению мыши. Это снимает главный раздражитель:
  панель вылезает, когда трогаешь мышь, чтобы убрать её. **Взять:** зоны активации снизу и сверху.
- *ModernZ:* у одной кнопки три действия: ЛКМ, ПКМ, СКМ. Например, ЛКМ по кнопке субтитров переключает,
  ПКМ открывает меню. **Взять:** ПКМ по кнопкам дорожек и скорости.
- *uosc flash-команды:* при перемотке с клавиатуры на секунду показывается только шкала, при громкости только громкость.
  **Взять:** контекстные мини-OSD вместо общего тоста.
- *Celluloid и IINA:* плавающая шапка с названием поверх видео, экран загрузки при буферизации.
  mpv 0.41 добавил индикатор буферизации в OSC.

**Меню и панели**
- *uosc:* у любого меню (дорожки, плейлист, главы, файлы) поиск начинается с набора текста.
  **Взять:** type-to-filter во всех попапах Lumen, особенно в списке субтитров на 30+ языков.
- *mpv 0.41 `context_menu.lua`, mpv.net:* полное контекстное меню по ПКМ. В Lumen меню есть, нужно проверить, что оно
  полное: дорожки, скорость, аспект, PiP, снимок.
- *Spotify:* правая панель Now Playing (трек, тексты, очередь, «недавно играло»). Урезание полноэкранной очереди до
  боковой панели вызвало много жалоб. **Взять:** очередь в двух видах, панелью и полной страницей.
- *IINA:* боковая панель быстрых настроек (видео, аудио, субтитры) по вкладкам поверх видео. Это близко к тому, что у
  Lumen уже есть.

**Медиатека**
- *Infuse и Plex:* постеры, фоновый арт, сезоны и серии, бейдж «Новое», прогресс на карточке, «Следующая серия»,
  экстра-материалы, пропуск интро и титров по community-базам (IntroDB). **Взять:** группировку сериалов по
  S01E02 и `Season N` (уже в бэклоге STATUS), полоску прогресса и «осталось 12 мин» на карточке.
- *MusicBee и foobar2000:* шапка альбома (обложка, год, жанр, длительность), колонка номера трека, многозначные
  исполнители (foobar 2.25), Auto DJ, вертикальный тег-редактор. Lumen файлы не меняет, поэтому тег-редактор не нужен
  или нужен только с явным подтверждением.
- *WMP 2026:* баннер «Индексируем медиатеку…» объясняет, почему не всё видно. **Взять:** прогресс сканирования.
- *VLC 4 nightly:* стартовый экран с миниатюрами видео из «Видео» (подход как у Lumen), но с жалобами на долгую загрузку.

**Жесты**
- *Screenbox:* жесты перемотки и громкости (свайп или перетаскивание по видео). *IINA:* трекпад: щипок для полного
  экрана, двумя пальцами перемотка. **Взять:** горизонтальная прокрутка тачпадом = перемотка (настройка
  `controls.hwheel` уже есть), щипок = зум или полный экран через `WM_GESTURE`/Pointer API, удержание ЛКМ = 2x.
- *Stremio:* задержка субтитров кнопками ±100 мс с ускорением при удержании.

## 5. Производительность

**Известные цифры**

| Плеер | Что известно |
|---|---|
| **Lumen** | Окно за ~80–110 мс. CPU: 1080p ~3% одного ядра, пауза ~0,4%, медиатека 0%. Закрытие 0,1–0,26 с. Пик RAM на 4K ~1,3 ГБ (P22). Дистрибутив: `Lumen.exe` 3,7 МБ + `libmpv-2.dll` **121 МБ** (несжатый). |
| VLC | Анекдот с форума: 4K на i5-7600 + GTX 1070 ~100% CPU (программный декодер); MPC-HC ~20%, PotPlayer ~55% на той же машине. Причина — hwdec. |
| WMP (2022–2026) | Тесты Digital Trends: больше RAM и медленнее открывает локальное видео, чем классический WMP. |
| Apple Music (Win) | Зависания на секунды при просмотре медиатеки, поиск >15 с (MacStories). |
| uosc | Документация признаёт вялость UI, потому что редрав OSD привязан к fps видео. Lumen рисует UI отдельно через DirectComposition, это его архитектурное преимущество. |
| foobar2000 | Пользователи отмечают <1% CPU при проигрывании и мгновенную медиатеку. |

Контролируемых сравнений RAM и времени старта между плеерами в 2025–2026 годах найти не удалось. Если Lumen
опубликует свои замеры (`LUMEN_TRACE`), это будет конкурентным преимуществом.

**Приёмы лидеров, применимые к Lumen**
1. **Zero-copy аппаратное декодирование.** mpv 0.41 предпочитает non-copy hwdec. В Lumen `BuildVf` отправляет кроп,
   резкость (`cas`), deblock и явный метод деинтерлейса через lavfi. Комментарий в коде сам говорит, что это
   копирует кадры в RAM. Замены:
   - кроп → свойство `video-crop`, оно уже используется в LiveState;
   - резкость → GLSL CAS через `glsl-shaders` или `--sharpen` в gpu-next;
   - деинтерлейс → `deinterlace=yes`, тогда с d3d11va mpv сам берёт `d3d11vpp`;
   - deblock оставить, но предупреждать о стоимости.
2. **hwdec.** `auto-safe` правильный выбор. Стоит дать в настройках явный `d3d11va` (zero-copy) против `d3d11va-copy`
   и показывать в статистике, что выбралось. mpv 0.41 предпочитает Vulkan hwdec, только если `gpu-api=vulkan`.
3. **Превью seekbar.** Сейчас: отдельный mpv, `hwdec=no`, `hr-seek=yes`, кадр через encode в rawvideo, 2 потока.
   thumbfast держит один постоянный процесс, генерирует по требованию и умеет стартовать при загрузке файла
   (`spawn_first`). Рекомендации:
   - искать по ключевым кадрам (`hr-seek=no`) при грубом наведении, точный кадр только после остановки курсора;
   - добавить `vd-lavc-skiploopfilter=all`, `vd-lavc-fast`, `sws-scaler=fast-bilinear`;
   - кэшировать кадры по сетке (например, каждые N секунд) и предзагружать соседние;
   - для 4K HEVC/AV1 попробовать `hwdec=d3d11va-copy` в превью-процессе.
4. **Кэш шейдеров.** `gpu-shader-cache` по умолчанию включён и важен для D3D11/ANGLE. Убедиться, что `config-dir`
   или `gpu-shader-cache-dir` указывают в постоянную папку. В gpu-next кэш сам чистится при 1,5 ГиБ.
   Первый запуск после обновления драйвера медленнее, это нормально.
5. **Память.** Пик 1,3 ГБ на 4K складывается из `demuxer-max-bytes` (по умолчанию 150 МиБ), `demuxer-max-back-bytes`,
   очереди кадров (swapchain depth 2 в 0.41) и превью-процесса. Для локальных файлов можно снизить
   `demuxer-max-bytes` до ~50 МиБ, кэш оставить для сети. Скрытый пункт `network.cacheLocal` как раз про это.
6. **Ленивая инициализация.** Lumen уже показывает окно до `mpv_initialize` (`init_` поток). Ещё можно:
   - создавать превью-процесс только при первом наведении на seekbar;
   - не грузить libass до первого субтитра (mpv так и делает);
   - отложить сканирование медиатеки до простоя (`scanOnStartup` уже настраивается).
7. **OSD/редрав.** mpv 0.41 перестал перерисовывать неизменный OSD, у Lumen OSD свой. Важно не будить композицию
   по таймеру на паузе (сейчас 0,4% CPU, хорошо) и не перерисовывать seekbar чаще 4–10 Гц для `time-pos`.
8. **Размер дистрибутива.** `libmpv-2.dll` 121 МБ. Это полная сборка shinchiro со всеми библиотеками. Варианты:
   UPX/zip-упаковка в установщике; облегчённая сборка libmpv без лишних демуксеров и Vulkan, если он не нужен.
   Для сравнения, инсталляторы MPC-HC и VLC занимают десятки мегабайт. Цифры примерные, по сборкам не проверены.
9. **Жалобы на медлительность у конкурентов:**
   - VLC медленно перематывает файлы с длинным GOP (быстрый поиск по ключевым кадрам);
   - у VLC нет превью, разработчики называли его «сложным»;
   - WMP медленно открывает файлы;
   - медиатека Apple Music подвисает;
   - медиатека VLC 4 медленно загружается;
   - uosc вялый при низком fps видео.

## 6. Что пользователи ненавидят (и чего Lumen должен избегать)

1. **Реклама, бандлы, малварь.** KMPlayer (тулбары, реклама посреди фильма), GOM (реклама в бесплатной версии,
   малварь в 2019), PotPlayer (претензии к установщику). У Lumen этого нет. Стоит закрепить в README фразой
   «никакой рекламы и телеметрии».
2. **Навязанные редизайны без отката.** Plex 2025–2026: на Roku/Fire TV действия выросли с 2 кликов до ~6, вернули
   левую навигацию после бунта, музыку и фото вернули в основное приложение. VLC 4: критика «iOS-подобного»
   упрощения. Spotify: урезанная очередь. **Правило:** не убирать частые действия глубже, давать «классический» вид.
3. **Сброс настроек при обновлении.** PotPlayer сбрасывает пресеты декодера, Jellyfin Desktop 2.0 не перенёс
   настройки. **Правило:** миграция `settings.json` с версионированием и резервная копия (в бэклоге N-03).
4. **Конфиг вместо GUI.** Главная претензия к mpv. Lumen уже решает её. Не стоит прятать нужное только в «Доп. параметры mpv».
5. **Тормоза медиатеки и поиска.** Apple Music, WMP, VLC 4. **Правило:** поиск по медиатеке <50 мс на 50k элементов,
   сканирование в фоне с прогрессом.
6. **Выход из полноэкранного режима между файлами плейлиста** (VLC). Стоит проверить, что Lumen так не делает.
7. **Неочевидные клавиши** (VLC: Shift+стрелки). Lumen близок к VLC/MPC. Стоит добавить пресет раскладки
   «YouTube» (J/L ±10 с, K пауза, F полный экран, M звук), как в Screenbox.
8. **Платные кодеки** (WMP: HEVC за деньги, AC-3 убран в 24H2). У Lumen всё в libmpv. Об этом стоит говорить в README.
9. **Жёсткое удаление по Delete** (mpv-hero). Только корзина и подтверждение.
10. **Нестабильный звук** (Apple Music: обрывы, громкость; MusicBee: заикания в WASAPI/ASIO). При добавлении exclusive
    режима нужен откат на shared, если устройство занято.

## 7. Приоритизированный список доработок

Обозначения сложности: S — до дня, M — несколько дней, L — неделя и больше.
🌐 — нужен сетевой сервис, 📦 — нужен внешний бинарь. Такие пункты собраны отдельно в 7.4.

### 7.1. P0: сильнее всего влияют на «лучший плеер»

| # | Что сделать | Почему | Сложн. | Файлы |
|---|---|---|---|---|
| P0-1 | **Убрать копирование кадров в RAM.** Кроп → `video-crop`; резкость → GLSL CAS (`glsl-shaders`, шейдер в ресурсах) вместо lavfi `cas`; деинтерлейс по умолчанию через `deinterlace=yes` (d3d11vpp), lavfi только по явному выбору метода с пометкой «нагружает CPU». | 4K и HDR без лишнего CPU, ниже пик памяти; mpv 0.41 сам предпочитает non-copy hwdec | M | `src/core/SettingsSchema.cpp` (`BuildVf`), `src/app/player/LiveState.cpp`, `src/Player.cpp` |
| P0-2 | **Аудио для домашнего кинотеатра и музыки:** passthrough (`audio-spdif` с чекбоксами AC3/E-AC3/DTS/DTS-HD/TrueHD), exclusive WASAPI (`audio-exclusive`), gapless (`gapless-audio`, `prefetch-playlist=yes` для аудио-очереди). | Есть у всех серьёзных конкурентов (MPC, PotPlayer, foobar, MusicBee); у Lumen нет вовсе; всё делается опциями mpv | S | `src/core/SettingsSchema.cpp`, `src/app/strings/settings.inc`, `src/Player.cpp` |
| P0-3 | **Оверлей статистики** (реализовать скрытый «Режим разработчика»): декодер и hwdec (zero-copy или нет), vo, разрешение, fps, пропуски кадров, битрейт, кэш, цветовое пространство и HDR-метаданные. Свойства `hwdec-current`, `frame-drop-count`, `estimated-vf-fps`, `video-params`, `demuxer-cache-state`. Горячая клавиша (`Ctrl+Shift+I` или `I`). | Есть у MPC (Ctrl+J), mpv (`i`), PotPlayer (Tab), VLC; без него пользователи не верят, что HDR/hwdec работают | M | `src/app/player/PlayerView.cpp`, `src/app/player/Panels.cpp`, `src/app/Actions.cpp`, `src/core/SettingsSchema.cpp` |
| P0-4 | **Быстрые превью на seekbar:** постоянный превью-процесс, поиск по ключевым кадрам при движении и точный кадр при остановке, `skiploopfilter`/`vd-lavc-fast`, сетка кэша и предзагрузка соседей, ленивый старт при первом наведении, `d3d11va-copy` для 4K HEVC/AV1. | Превью — главный «вау» против VLC и WMP, но на 4K оно дорогое; thumbfast задаёт планку | M | `src/app/player/SeekPreview.cpp`, `src/app/player/SeekPreview.h`, `src/library/Thumbnails.cpp` |
| P0-5 | **Поиск субтитров онлайн** 🌐 (OpenSubtitles REST): поиск по хэшу файла и имени, выбор языка из `subtitles.language`, скачивание рядом с файлом или в кэш, запоминание пары (`subtitle-pairs.json` уже есть). Ключ API приложения, необязательный логин пользователя. Учесть лимиты: ~5 загрузок/IP/сутки без логина, больше с аккаунтом. | Самая ожидаемая функция у MPC-HC, MPC-BE, SMPlayer, uosc, IINA; у Lumen её нет | L | `src/app/dialogs/AddSubtitles.cpp`, `src/app/player/TracksPanel.cpp`, новый `src/library/SubtitleSearch.cpp`, `src/core/SettingsSchema.cpp` (приватность: явное включение) |
| P0-6 | **Сериалы и «Следующая серия»:** распознавание S01E02/1x02/«Серия 2», группировка по сезонам в «Видео», карточка «Следующая серия» на главной, предложение следующей серии за N секунд до конца (как Stremio/Infuse), перенос выбранных языков аудио и субтитров на следующую серию. | Главный UX медиацентров, у десктоп-плееров его почти нет; уже в бэклоге STATUS | L | `src/library/MediaLibrary.cpp`, `src/library/MediaTypes.cpp`, `src/library/History.cpp`, `src/app/library/LibHome.cpp`, `src/app/library/Views.cpp`, `src/app/player/PlayerView.cpp` |

### 7.2. P1: заметно поднимают уровень

| # | Что сделать | Почему | Сложн. | Файлы |
|---|---|---|---|---|
| P1-1 | **Качество изображения:** пресеты «Быстро / Сбалансировано / Максимум» (`scale`/`cscale`/`dscale`, `profile=high-quality`), отдельный пункт апскейлера; tone-mapping в Настройках (алгоритм, `hdr-compute-peak`, `target-peak`, `gamut-mapping-mode`), debanding вкл/выкл. | Уровень MPC+MPCVR и PotPlayer без madVR; всё через свойства gpu-next | M | `src/core/SettingsSchema.cpp`, `src/app/player/SettingsPanel.cpp`, `src/app/player/LiveState.cpp` |
| P1-2 | **RTX/Intel Super Resolution и NVIDIA TrueHDR:** переключатель «ИИ-апскейл (NVIDIA/Intel)» → `vf=d3d11vpp=scale=2:scaling-mode=nvidia\|intel`, `nvidia-true-hdr`; доступен только при `gpu-api=d3d11` и hwdec d3d11va; автоотключение на других GPU. | Есть в VLC 3.0.21+, PotPlayer, MPCVR; это маркетинговая функция для владельцев RTX | M | `src/core/SettingsSchema.cpp` (`BuildVf`), `src/app/player/SettingsPanel.cpp` |
| P1-3 | **Интерполяция кадров как обычная функция:** «Плавное движение» = `video-sync=display-resample` + `interpolation=yes` + `tscale=oversample`/`linear`, перенести из «Разработчика» в «Видео». | Любимая функция SVP/mpv-пользователей; бесплатная альтернатива SVP | S | `src/core/SettingsSchema.cpp`, `src/app/player/SettingsPanel.cpp` |
| P1-4 | **Type-to-filter во всех меню и попапах** (дорожки, главы, очередь, языки), как в uosc. | Субтитры на 30+ языков и длинные очереди неудобно листать | M | `src/ui/Controls.cpp`, `src/app/player/TracksPanel.cpp`, `src/app/player/QueuePanel.cpp` |
| P1-5 | **Мышь и жесты:** колесо над громкостью или скоростью меняет значение, ПКМ по ним сбрасывает; колесо над seekbar перематывает; удержание ЛКМ или клавиши = временно 2x; щипок тачпадом = зум или полный экран; «тонкий прогресс» при скрытой панели; проксимити-зоны показа панели. | Самые любимые микро-паттерны uosc, ModernZ, YouTube и Screenbox | M | `src/app/player/ControlBar.cpp`, `src/app/player/PlayerView.cpp`, `src/app/Actions.cpp`, `src/core/SettingsSchema.cpp` |
| P1-6 | **Музыка:** crossfade (своя реализация через `af` fade + предзагрузка), `dynaudnorm` как режим нормализации, тексты песен из `.lrc` рядом с файлом и тега `LYRICS`/`SYLT` с синхронной прокруткой на экране музыки; шапка альбома и исполнителя (уже в бэклоге). | Spotify, MusicBee и Dopamine задали ожидания; локальные LRC не требуют сети | M | `src/app/player/MusicView.cpp`, `src/library/MediaLibrary.cpp`, `src/app/library/LibMusic.cpp`, `src/core/SettingsSchema.cpp` (`BuildAf`) |
| P1-7 | **Субтитры:** подложка-фон как стиль в Настройках, отступ снизу, «в чёрных полях», `sub-blur`, стиль и позиция вторичных субтитров (сверху), опция «как в Windows» (системный стиль субтитров), автокопирование строки в буфер, внешние аудиодорожки (`audio-file-auto=fuzzy`). | Закрывает разрыв с MPC-HC 2.8 и WMP 2026; дешёво | S | `src/core/SettingsSchema.cpp`, `src/app/player/TracksPanel.cpp`, `src/app/player/LiveState.cpp` |
| P1-8 | **Реализовать скрытые простые пункты:** длинный шаг перемотки, компактный режим, пауза при захвате аудио другой программой (Windows ducking), `network.cacheLocal` (меньше кэша на локальных файлах, это и P22), громкость до 200%. | Убирает «мёртвые» места в схеме; часть влияет на память | S–M | `src/core/SettingsSchema.cpp`, `src/Player.cpp`, `src/app/SystemIntegration.cpp` |
| P1-9 | **Пресет клавиш «YouTube»** (J/K/L, 0–9 = 0–90% длительности, `<`/`>` скорость, C субтитры) в редакторе горячих клавиш. | Снимает жалобу «неочевидные клавиши»; так сделал Screenbox | S | `src/app/Actions.cpp`, `src/app/settings/ShortcutStore.cpp`, `src/app/settings/SettingsShortcuts.cpp` |
| P1-10 | **Устойчивость данных:** версия схемы и миграция `settings.json`, резервная копия `history.json`/`library.json` при повреждении (N-03), экспорт и импорт настроек. | Пользователи ненавидят сброс настроек (PotPlayer, Jellyfin 2.0) | S | `src/core/*` (Config), `src/library/History.cpp`, `src/library/MediaLibrary.cpp` |

### 7.3. P2: дифференциация и «вкусности»

| # | Что сделать | Почему | Сложн. | Файлы |
|---|---|---|---|---|
| P2-1 | Пользовательские GLSL-шейдеры: папка `shaders\`, список в быстрых настройках, без перезапуска (`glsl-shaders-append`/`change-list`). | FSRCNNX и Anime4K — культовая функция mpv | S | `src/core/SettingsSchema.cpp`, `src/app/player/SettingsPanel.cpp` |
| P2-2 | Пропуск интро и титров: по главам с именами Opening/Intro/Credits/«Заставка» (локально) и ручные метки «интро до 1:30» на сериал. | Infuse 8.4 и Plex; локальная версия не требует сети | M | `src/app/player/PlayerView.cpp`, `src/library/Bookmarks.cpp` |
| P2-3 | «Удалить и следующий» (в корзину, с подтверждением), «Открыть в Проводнике», «Переименовать играющий файл» (VLC 3.0.22 разрешил это для играющих файлов). | Любят в uosc и для сериалов | S | `src/app/Actions.cpp`, `src/app/player/PlayerView.cpp` |
| P2-4 | Настройки на каждый файл (аспект, задержки, дорожки, скорость), как в SMPlayer. Опция «запоминать для этого файла». | Полезно для коллекций с кривыми задержками | M | `src/Player.cpp` (`watch-later-options`), `src/library/History.cpp` |
| P2-5 | Визуализация для музыки (реализовать скрытую `audio.visualizer`): спектр по PCM через `af=lavfi=[showspectrum]` нельзя (дорого), лучше свой FFT по `audio-out` через scripting или WASAPI loopback. | Есть у VLC, PotPlayer, foobar, MusicBee | M | `src/app/player/MusicView.cpp` |
| P2-6 | Прогресс сканирования медиатеки (баннер, как в WMP 2026), бейджи «Новое» и «Битый файл» (P19). | Объясняет пустые разделы; закрывает бэклог | S | `src/app/library/LibHome.cpp`, `src/app/library/ItemGrid.cpp`, `src/library/MediaLibrary.cpp` |
| P2-7 | Клик по слову в субтитрах: копировать или открыть словарь (PotPlayer). | Ниша изучающих языки, хорошая дифференциация | M | `src/app/player/PlayerView.cpp` (`sub-text` + геометрия) |
| P2-8 | Скины и токены оформления (скрытые `interface.customSkin`): JSON поверх `design/DESIGN.md`. | Кастомизацию ценят у PotPlayer и ModernZ; при этом монохромный бренд — плюс | M | `src/ui/Theme.cpp`, `src/core/SettingsSchema.cpp` |
| P2-9 | Облегчённая сборка libmpv (121 МБ → цель <60 МБ) или сжатие в установщике. | Размер загрузки и холодный старт с HDD | M | `tools/fetch-libmpv.ps1`, `build.bat` |

### 7.4. Пункты, которым нужна сеть 🌐 или внешний бинарь 📦

| # | Что | Зависимость | Приоритет | Сложн. | Файлы |
|---|---|---|---|---|---|
| N-1 | Поиск субтитров OpenSubtitles (см. P0-5) | 🌐 OpenSubtitles REST API: ключ разработчика, лимиты загрузок, VIP ~$15/год | P0 | L | `src/app/dialogs/AddSubtitles.cpp`, новый модуль |
| N-2 | Ссылки YouTube и прочих сайтов «из коробки»: поставка или скачивание `yt-dlp.exe` по кнопке, автообновление (yt-dlp часто ломается без обновлений), выбор качества (`playback.videoQuality` уже есть), вставка из буфера. | 📦 yt-dlp (+ffmpeg для слияния, опционально) | P1 | M | `src/core/SettingsSchema.cpp` (`network.ytdl`), `src/app/dialogs/Dialogs.cpp`, `src/Player.cpp` |
| N-3 | ИИ-субтитры и перевод на устройстве (Whisper/whisper.cpp, faster-whisper), как PotPlayer и анонс VLC. | 📦 модель (сотни МБ) + бинарь; GPU | P2 | L | новый модуль, `src/app/player/TracksPanel.cpp` |
| N-4 | Скробблинг last.fm/ListenBrainz (скрытые пункты есть). | 🌐 last.fm API | P2 | M | `src/core/SettingsSchema.cpp`, `src/app/SystemIntegration.cpp` |
| N-5 | Веб-пульт для телефона (скрытые пункты есть; MPC-HC 2.8.1 сделал страницу для телефона). | 🌐 локальный HTTP-сервер, пароль | P2 | M | новый модуль, `src/core/SettingsSchema.cpp` |
| N-6 | DLNA/UPnP: обзор серверов и рендер на ТВ (foobar 2.25, VLC, Screenbox); Chromecast (VLC, Screenbox). | 🌐 SSDP/UPnP; для Chromecast — протокол Cast + перекодирование | P2 | L | `src/app/library/LibNetwork.cpp`, новый модуль |
| N-7 | Метаданные фильмов и сериалов (постеры, описания) через TMDB, явное включение (`privacy.fetchMetadata` уже есть). | 🌐 TMDB API | P2 | M | `src/library/MediaLibrary.cpp`, `src/app/ImageCache.cpp` |
| N-8 | Тексты песен из сети (LRCLIB — открытая база синхронных текстов). | 🌐 LRCLIB | P2 | S | `src/app/player/MusicView.cpp` |
| N-9 | Интеграция SVP (обнаружение установленного SVP 4.7). | 📦 SVP (платный) | P2 | S | `src/Player.cpp` (`input-ipc-server`) |

## 8. Источники

**mpv и оболочки**
- mpv 0.41.0 release notes: https://sources.debian.org/src/mpv/0.41.0-2/RELEASE_NOTES
- mpv version history: https://www.videohelp.com/software/mpv-media-player/version-history
- uosc: https://github.com/tomasklaen/uosc
- ModernZ: https://github.com/Samillion/ModernZ
- mpv.net: https://github.com/mpvnet-player/mpv.net ; https://videohelp.com/software/mpv-net/version-history
- thumbfast: https://github.com/po5/thumbfast
- Кэш шейдеров mpv (документация в коммите): https://git.tjdev.de/mirror/mpv/commit/69891c4070f03236c3843fd49e6ee69a46dfe82c
- RTX VSR в mpv: https://www.svp-team.com/forum/viewtopic.php?id=7381 ; https://ithelp.ithome.com.tw/articles/10402152 ; https://git.quad4.io/Mirrors/mpv/commit/6ca3752f758c1e5452f90348993fc2b2653aaa2b
- mpv-hero (Delete удаляет файл безвозвратно): https://forum.doom9.org/showthread.php?p=2007893

**MPC, VLC, PotPlayer и другие видеоплееры**
- MPC-HC releases: https://github.com/clsid2/mpc-hc/releases
- MPC-HC 2.8.2 (ReplayGain): https://www.techspot.com/news/113890-mpc-hc-282-adds-replaygain-support-playback-improvements.html
- MPC-BE 1.8.x: https://www.neowin.net/amp/mpc-be-183/ ; https://www.neowin.net/amp/mpc-be-185/ ; https://videohelp.com/tools/MPC-BE
- VLC 3.0.22/3.0.23: https://www.omgubuntu.co.uk/2025/09/vlc-player-update-2025 ; https://www.videohelp.com/software/VLC-media-player/version-history
- VLC ИИ-субтитры (CES 2025): https://alternativeto.net/news/2025/1/vlc-teases-offline-and-real-time-ai-powered-subtitles-and-translation-at-ces-2025/ ; https://www.techradar.com/computing/artificial-intelligence/ai-will-start-subtitling-your-videos-thanks-to-vlc
- VLC 4 (превью интерфейса): https://9to5mac.com/2021/02/16/vlc-4-user-interface/ ; https://www.ghacks.net/?p=149996
- Жалобы на VLC: https://wiki.videolan.org/Usability_Complains ; https://forum.videolan.org/viewtopic.php?p=474693 ; https://forum.videohelp.com/showthread.php?p=2596879
- PotPlayer: https://www.alternativeto.net/software/daum-potplayer/about/ ; https://www.neowin.net/software/potplayer-1720538/ ; https://en.kocpc.com.tw/?p=11281 ; https://screenapp.io/blog/best-video-players-with-ai-subtitles
- SMPlayer 25.6: https://www.majorgeeks.com/files/details/smplayer_64_bit.html
- Celluloid 0.27: https://alternativeto.net/news/2024/6/celluloid-0-27-released-with-improved-floating-mode-user-interface-and-faster-launch-times
- IINA: https://iina.io/ ; https://iina.io/release-note/1.4.0.html ; https://github.com/iina/iina
- Infuse 8 и 8.4: https://firecore.com/blog/infuse-8-now-available ; https://alternativeto.net/news/2026/3/infuse-8-4-introduces-extras-intro-skipping-home-screen-favorites-and-improved-playback
- Windows Media Player (2022–2026): https://en.wikipedia.org/wiki/Windows_Media_Player_(2022) ; https://es.digitaltrends.com/computadoras/el-reproductor-multimedia-2026-de-windows-11-es-peor-que-el-de-hace-17-anos/ ; https://www.ithome.com/0/964/660.htm
- Screenbox: https://alternativeto.net/software/screenbox/about ; https://apps.microsoft.com/detail/9ntsnmsvcb5l?hl=en-US
- KMPlayer и GOM: https://en.wikipedia.org/wiki/GOM_Player ; https://www.snapfiles.com/users/631025/myreviews.html
- SVP 4.7: https://www.videohelp.com/software/SmoothVideo-Project/version-history ; https://www.svp-team.com/forum/viewtopic.php?id=8085

**Медиацентры**
- Kodi 22 Piers: https://betanews.com/article/kodi-22-piers-rc-1-features/ ; https://linuxiac.com/kodi-22-preview-shows-ffmpeg-8-upgrade-and-new-format-support/
- Jellyfin Desktop 2.0: https://alternativeto.net/news/2025/12/jellyfin-desktop-2-0-launches-with-rebranding-qt-6-upgrade-and-fresh-features
- Jellyfin и Plex: https://www.ctrl.blog/entry/jellyfin-vs-plex/
- Plex, откат редизайна: https://neowin.net/news/plex-reverts-tv-app-layout-after-intense-user-backlash/ ; https://piunikaweb.com/2026/05/04/plex-fire-tv-redesign-unusable-users-slam-navigation/ ; https://forums.plex.tv/t/new-ui-is-an-awful-experience/931048?page=23
- Stremio: https://alternativeto.net/news/2026/9/stremio-brings-live-tv-guide-interface-zoom-enhanced-video-player-and-windows-vlc-option/ ; https://www.mintlify.com/stremio/stremio-web/features/player

**Музыкальные плееры**
- foobar2000 2.25: https://www.foobar2000.org/changelog-mac ; https://www.neowin.net/amp/foobar2000-2252/
- MusicBee 3.6: https://www.neowin.net/software/musicbee-369403/ ; https://www.majorgeeks.com/files/details/musicbee.html
- Dopamine 3.0: https://www.neowin.net/software/dopamine-30/ ; https://www.neowin.net/software/dopamine-305/
- foobar2000 и MusicBee (отзывы): https://gummysearch.com/tools/best-products/music-players/ ; https://hydrogenaudio.org/index.php?msg=876393 ; https://forums.whathifi.com/threads/music-player.100057/latest
- Apple Music на Windows: https://www.notebookcheck.net/Apple-Music-for-Windows-Why-its-garbage-listening-experience-has-been-failing-loyal-users-since-2023.1110071.0.html ; https://www.macstories.net/stories/hands-on-with-apple-music-for-windows/
- Spotify: https://newsroom.spotify.com/?p=23515 ; https://community.spotify.com/t5/Live-Ideas/Return-expanded-queue-view-not-only-sidebar-for-desktop-as-it/idi-p/5924728/page/4 ; https://support.spotify.com/us/article/Crossfade-feature/ ; https://community.spotify.com/t5/FAQs/Automix-Overview/ta-p/5257278/highlight/true

**Субтитры и сервисы**
- Лимиты OpenSubtitles: https://community.firecore.com/t/opensubtitles-reducing-limit-from-200-per-day-to-10/52109 ; https://forum.jellyfin.org/t-opensubtitles-limitations?pid=45923 ; https://redlib.hbubli.cc/r/learnprogramming/comments/1bd0rxx/using_subtitles_api

**Производительность и мнения пользователей**
- 4K CPU (анекдот): https://forums.anandtech.com/threads/4k-movies-uses-a-lot-of-cpu.2515619 ; https://forum.videohelp.com/showthread.php?p=2551827
- Reddit, сравнения плееров (по сниппетам поиска): https://www.reddit.com/r/potplayer/comments/1supw92/potplayer_vs_vlc_vs_mpv/ ; https://www.reddit.com/r/VLC/comments/1mxz7sw/is_mpchc_better_than_vlc/ ; https://www.reddit.com/r/Piracy/comments/1vn5j71/why_everyone_prefer_vlc_than_mpchc/ ; https://www.reddit.com/r/potplayer/comments/1pn4igb/why_do_you_use_potplayer/

**Оговорки:**
- Reddit не удалось открыть напрямую, его мнения взяты из сниппетов поиска.
- Ячейки таблицы с «?» и часть «ч» основаны на общих знаниях о продуктах. Их стоит перепроверить перед публичным сравнением.
- Размеры инсталляторов конкурентов указаны примерно.

---

## Приложение А. Инвентарь Lumen

**Ядро:** libmpv, `vo=gpu-next`, `hwdec=auto-safe` (по умолчанию), свой UI на Direct2D/DirectComposition, окно за ~80–110 мс,
CPU на 1080p ~3% ядра, пик памяти на 4K ~1,3 ГБ (P22).

**Плеер:** seekbar с превью кадров (отдельный экземпляр mpv, 192×108) и главами, A-B петля, очередь, закладки, дорожки
(видео/аудио/субтитры, внешние файлы, вторичные субтитры через `secondary-sid`), быстрые настройки (скорость, изображение,
эквалайзер, задержки, стиль субтитров, tone-mapping, зум/пан/поворот/кроп), PiP, мини-плеер, экран музыки, снимки,
запись потока, карточка ошибки.

**Медиатека:** главная (продолжить, недавние, частые, избранное), видео по папкам, музыка (песни/альбомы/исполнители/жанры),
сеть и радио, плейлисты (m3u/m3u8/pls/cue/xspf), история.

**Настройки (~190 видимых):** тема/акцент/язык; окно (автоподгон, поверх всех, без рамки); автоскрытие; OSD; трей; SMTC;
приватность и исключения; шаги перемотки; мышь (клик, двойной клик, колесо, гор. колесо); resume; скорость; ytdl-format;
дорожки по умолчанию; повтор/перемешивание/папка как плейлист; hr-seek; HDR-вывод (`target-colorspace-hint`, trc/prim);
hue/brightness/contrast/saturation/gamma/sharpen/deband/grain; деинтерлейс; аспект/кроп; снимки (формат, шаблон);
аудиоустройство, каналы, громкость до 150%, loudnorm (EBU R128), ночной режим, ReplayGain трек/альбом + preamp,
эквалайзер с пресетами, «объёмный звук» (наушники/комната), stereowiden, коррекция высоты; языки аудио/субтитров;
кодировка, автозагрузка субтитров (+подпапки), стиль субтитров (шрифт, размер, цвет, контур, тень, позиция, override ASS);
одно окно / очередь; кэш, прокси, TLS, таймаут, user-agent, yt-dlp (выкл. по умолчанию, бинарь не поставляется);
hwdec, постобработка, skiploopfilter, потоки, framedrop, приоритет; video-sync, interpolation, vo, gpu-api; скрипты mpv
и script-opts; доп. параметры mpv.

**Скрыто как нереализованное (`.Hidden()`):** режим разработчика (статистика поверх видео), формат лога, сбор статистики,
скины, «видео в главном окне», компактный режим, last.fm, веб-пульт, длинный шаг перемотки, пауза при захвате аудио
другой программой, язык меню DVD, видео как обои, визуализация, кэш для локальных/дисков/захвата, шифры, encoder/decoder,
jitter/averaging, запись без перекодирования, трансляция (cast) и стриминг, «использовать медиатеку».

**В коде нет вовсе:** gapless/crossfade-настроек, exclusive WASAPI (`audio-exclusive`), passthrough (`audio-spdif`),
выбора апскейлера (`scale`, `cscale`, `dscale`), настроек tone-mapping в Настройках (только быстрый переключатель),
ICC-профиля как настройки, поиска субтитров онлайн, текстов песен (LRC), DLNA/UPnP, Chromecast, scrobbling,
`audio-file-auto` (внешние аудиодорожки), `dynaudnorm`.

**Горячие клавиши:** ~50 действий, все переназначаемые; набор в духе VLC/MPC (Space, ←/→ 10 с, Shift 3 с, Ctrl 1 мин,
`.`/`,` кадр, `[`/`]` скорость, `A`/`V` дорожки, `K/J` и `Shift+H/G` задержки, `F`, `Ctrl+Shift+P` PiP, `Ctrl+I` инфо).

---

## Приложение Б. Рабочие заметки (сырые находки)

- **mpv 0.41.0 (дек. 2025):** `gpu-next` по умолчанию; предпочтение Vulkan hwdec и non-copy hwdec; `--hwdec-threads`;
  `context_menu.lua` (правый клик); `select.lua`; индикатор буферизации в OSC; HDR: reference white, sub peak в HDR,
  `target-colorspace-hint=auto`, `blend-subtitles=video` в gpu-next; d3d11 composition mode; swapchain depth 2;
  кэш libass (`glyph-limit`, `bitmap-max-size`); OSD не перерисовывается без изменений; track-add с флагами default/forced.
- **MPC-HC (clsid2) 2.7.4–2.8.3 (июль–окт. 2026):** MPC Video Renderer по умолчанию с HDR; автоподбор рендера/hwdec
  по GPU при первом запуске; таймер рядом с seekbar; ReplayGain (2.8.2); редактор клавиш без мыши; вторичные субтитры
  (SRT, сверху); автокопирование текста субтитров в буфер; веб-пульт для телефона; Win11-тема и нативные меню;
  HLG tonemapping; диалог управления историей; OpenSubtitles и yt-dlp были раньше.
- **VLC 3.0.21–3.0.23 (2024–2026):** Super Resolution (AMD/NVIDIA RTX VSR), NVIDIA TrueHDR (SDR→HDR), Opus Ambisonic,
  ARM64, тёмная палитра Qt, можно переименовывать/удалять играющий файл. VLC 4.0 так и не вышел стабильным.
- **MPC-BE 1.8.x:** встроенный парсер YouTube + yt-dlp, предпочтительный аудиоформат, язык по умолчанию.
- **PotPlayer:** живые субтитры из звука (распознавание речи), кликабельные слова в субтитрах, YTT-субтитры,
  внешние файлы глав; жалобы на рекламу/adware в установщике, сброс пресетов декодера при обновлении.
- **uosc:** «проксимити»-UI (элементы появляются при приближении курсора), таймлайн сворачивается в тонкую полоску,
  главы как диапазоны, колесо над громкостью/скоростью меняет значение, ПКМ сбрасывает; меню с мгновенным поиском
  по вводу; поиск и скачивание с OpenSubtitles по хэшу (5 загрузок/день без аккаунта); файловый браузер;
  «удалить и следующий»; flash-команды (кратко показать таймлайн/громкость). Документация признаёт вялость UI,
  если редрав привязан к fps видео (лечится `video-sync=display-resample`).
- **ModernZ:** раскладки default/compact/mini/seekbar-only; иконки Fluent/Material; 4 стиля меток глав; ЛКМ/ПКМ/СКМ
  на кнопке = три разных действия; отдельная зона наведения для верхней панели; постоянная тонкая полоска прогресса.
- **mpv.net:** поиск по настройкам (conf editor) и по клавишам (input editor), темы, .NET-расширения, глобальные
  горячие клавиши, JSON IPC, быстрый старт.
- **IINA 1.4:** JS-плагины (встроенные Online Media, OpenSubtitles, User Scripts); превью по всей шкале;
  музыкальный режим (обложка, исполнитель), нативный PiP.
- **Infuse 8.4 (март 2026):** метаданные TMDB, постеры, актёры, трейлеры; фильтры по жанру/рейтингу/разрешению/
  просмотренности; коллекции; пропуск интро/титров/«ранее в сериале» (community-базы); экстра-материалы.
- **Windows Media Player (2022→2026):** стиль субтитров по системным настройкам Windows; баннер индексации медиатеки;
  жалобы: больше RAM и медленнее открывает видео, чем классический WMP; платные кодеки (HEVC), нет AC-3 в 24H2.
- **Screenbox (UWP + LibVLCSharp):** Fluent UI, жесты перемотки и громкости, YouTube-раскладка клавиш, PiP,
  Chromecast, обзор сети (DLNA/SMB), горячие клавиши размера окна. Пользователи хотят внешние аудиодорожки.
- **foobar2000 2.25 (Win, сент. 2025; Mac 2.25.7 — фев. 2026):** встроенный UPnP-рендерер в обе стороны (играть на
  устройство / быть управляемым), индексация сетевых шар FTP/WebDAV/SMB в медиатеке, FFmpeg 8 (xHE-AAC), многозначные
  исполнители/жанры. Классика: gapless, ReplayGain-сканер, WASAPI exclusive, DSP-цепочка, конвертер, компонентная
  архитектура, мгновенная медиатека на 100k+ треков.
- **MusicBee 3.6:** Auto DJ с фильтрами и весами по рейтингу/давности, тексты песен из сети (+MiniLyrics), вертикальный
  тег-редактор в боковой панели, автопоиск обложек и тегов.
- **Dopamine 3.0.x:** папки плейлистов, gapless, логарифмическая громкость, разбиение исполнителей по разделителям,
  last.fm, тексты, Discord RPC; 3.0.3 — crossfade и умные плейлисты; 3.0.5 — кнопки в превью на панели задач.
- **Apple Music на Windows:** жалобы на обрывы звука, сломанную громкость, синхронизацию, зависания на секунды при
  просмотре медиатеки, поиск >15 с (MacStories), нет клавиатурных сокращений для поиска.
- **Spotify desktop:** правая панель Now Playing (трек, исполнитель, тексты, транскрипты подкастов), очередь
  с «Недавно играло»; громкие жалобы, когда полноэкранную очередь урезали до боковой панели.
- **Производительность (анекдоты):** контролируемых сравнений RAM нет. Один пользователь (i5-7600 + GTX 1070, 4K):
  VLC ~100% CPU, PotPlayer ~55%, MPC-HC ~20% — разница в аппаратном декодировании; WMP (новый) открывает видео
  медленнее и ест больше RAM, чем классический.
- **RTX VSR / TrueHDR в mpv:** `--vf=d3d11vpp=scale=2:scaling-mode=nvidia` (VSR), `nvidia-true-hdr` (SDR→HDR);
  требует `gpu-context=d3d11` + `hwdec=d3d11va`; с Vulkan не работает. VLC 3.0.21 даёт то же для AMD/NVIDIA в GUI.
- **OpenSubtitles:** REST API требует ключ разработчика; лимиты плавают: ~5 загрузок/IP/сутки без логина, 10–20 с
  бесплатным аккаунтом, VIP ~$15/год. Поиск без лимита. Хэш-поиск (moviehash) даёт точные совпадения.
- **SMPlayer 25.6 (июнь 2025):** запоминание настроек на каждый файл (дорожки, субтитры, позиция), YouTube,
  поиск/скачивание субтитров с opensubtitles, генератор миниатюр-превью (contact sheet).
- **Celluloid 0.27:** плавающая шапка и контролы появляются только над видео, экран загрузки при буферизации,
  ускоренный старт, настройки не перезапускают mpv, Alt+стрелки = пан, Alt+± = зум.
