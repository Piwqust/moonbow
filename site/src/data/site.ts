// All visible content of the site.
// Assumptions (placeholder: true, the owner replaces them):
//   - url: the production domain is unknown;
//   - download.url / download.version: there is no public release yet;
//   - source.url: the repository has no public remote;
//   - the demo media on the screenshots (the film "Вечер у воды", the series "Тихий берег",
//     the artist "Сава Ветров") are made up; their frames and covers are Unsplash photos (credits below).
// Facts about Moonbow come from README.md, design/DESIGN.md and STATUS.md of the player.

export const site = {
  name: 'Moonbow',
  url: 'https://moonbow-player.example', // placeholder
  description:
    'Moonbow: видео- и аудиоплеер с медиатекой для Windows 10 и 11. Окно за 0,05 секунды, картинка без мигания, ваши файлы остаются на месте.',
  tagline: 'Плеер для своих фильмов, сериалов и музыки на Windows',
};

export const download = {
  placeholder: true,
  url: '/skachat/#sborka', // placeholder: link to a release when there is one
  version: '0.9', // placeholder
  size: 'около 30 МБ вместе с libmpv',
};

export const source = { placeholder: true, url: '#', label: 'Исходный код' }; // placeholder

export const nav = [
  { href: '/vozmozhnosti/', label: 'Возможности' },
  { href: '/klavishi/', label: 'Клавиши' },
  { href: '/skachat/', label: 'Скачать' },
];

// The evening on a logarithmic clock: each stop is a moment, t in seconds.
export type Stop = { t: number; unit: string; title: string; text: string; shot: string; alt: string; dark?: boolean };
export const stops: Stop[] = [
  {
    t: 0.05, unit: '50 миллисекунд', title: 'Окно уже на экране',
    text: 'Moonbow показывает окно с первым кадром видеокарты. Рамка плеера, имя файла и панель управления приходят сразу, без пустого серого прямоугольника.',
    shot: 'player-film', alt: 'Окно Moonbow за мгновение до картинки: заголовок файла и панель управления на месте, область видео ещё чёрная', dark: true,
  },
  {
    t: 0.2, unit: '200 миллисекунд', title: 'Картинка проявляется из чёрного',
    text: 'Кадр выходит из темноты за 200 мс, один раз и спокойно. Если файл открывается дольше, по полосе перемотки бежит тонкая линия, а не крутится колесо.',
    shot: 'player-film', alt: 'Фильм «Вечер у воды» в Moonbow: девушка смотрит на город на закате, внизу полоса перемотки и кнопки',
  },
  {
    t: 1, unit: '1 секунда', title: 'Полоса перемотки показывает кадр',
    text: 'Наведите на полосу, и над ней появится кадр из этого места и время. Колесо мыши над полосой перематывает, удержание кнопки включает 2×.',
    shot: 'player-seek-preview', alt: 'Над полосой перемотки Moonbow всплыло превью кадра с временем',
  },
  {
    t: 8, unit: '8 секунд', title: 'Песня перетекает в песню',
    text: 'Переход между треками длится от 1 до 12 секунд, а внутри альбома музыка идёт без пауз. Текст песни движется в такт: из .lrc рядом с треком, из тегов или из LRCLIB, если включить.',
    shot: 'music-lyrics', alt: 'Экран музыки в Moonbow: обложка альбома «Огни» и синхронный текст песни',
  },
  {
    t: 90, unit: '1,5 минуты', title: 'Заставку можно пропустить',
    text: 'Если в файле размечены главы, Moonbow узнаёт заставку и титры и предлагает их пропустить. Одна кнопка в углу, и она исчезает, когда не нужна.',
    shot: 'player-skip-intro', alt: 'Серия «Тихого берега» в Moonbow, в углу кнопка «Пропустить заставку»',
  },
  {
    t: 2520, unit: '42 минуты', title: 'Титры. Дальше следующая серия',
    text: 'Серия заканчивается, и Moonbow показывает карточку следующей. В медиатеке сериалы сами собираются по сезонам, без ручных плейлистов.',
    shot: 'player-next-episode', alt: 'На титрах серии Moonbow показывает карточку «Следующая серия»',
  },
  {
    t: 7200, unit: '2 часа', title: 'Каждый файл помнит свои настройки',
    text: 'Дорожка, субтитры, задержки, по желанию скорость и поворот: Moonbow запоминает их для каждого файла. Пробел после Стопа продолжает с места остановки.',
    shot: 'player-stats', alt: 'Фильм в Moonbow со статистикой поверх видео: декодер, кадры, кэш',
  },
  {
    t: 604800, unit: 'неделя', title: 'Через неделю всё на месте',
    text: 'На главной ждёт «продолжить просмотр» с пометкой, сколько осталось, следующая серия и недавние папки. Файлы на диске Moonbow никогда не перемещает и не меняет.',
    shot: 'home-continue', alt: 'Главная медиатеки Moonbow: ряд «Продолжить просмотр» с постерами и временем до конца',
  },
];

export const tones = [
  { name: 'Белый', hex: '#EDEDED' },
  { name: 'Жемчуг', hex: '#D2D2D2' },
  { name: 'Серебро', hex: '#B7B7B7' },
  { name: 'Сталь', hex: '#9C9C9C' },
  { name: 'Графит', hex: '#828282' },
];

export const facts = [
  { value: '0,05 с', label: 'до окна на экране' },
  { value: '0,085 с', label: 'до первого кадра' },
  { value: '≈240', label: 'настроек с поиском' },
  { value: '9', label: 'форматов тегов читает свой парсер' },
];

export const stack = ['C++20', 'Win32', 'Direct2D', 'DirectComposition', 'libmpv', 'GPL-3.0'];

export type FeatureGroup = { id: string; title: string; lead: string; items: string[] };
export const features: FeatureGroup[] = [
  {
    id: 'pleer', title: 'Плеер', lead: 'Всё, что нужно на просмотре, и ничего поверх картинки без спроса.',
    items: [
      'Полоса перемотки с быстрыми превью кадров и главами, отметки петли A-B',
      'Очередь, закладки, главы, воспроизведение без пауз между треками',
      'Видео-, аудио- и субтитровые дорожки, внешние файлы',
      'Быстрые настройки: скорость, качество изображения, эквалайзер, задержки, стиль субтитров',
      'Статистика поверх видео: декодер, работа без копирования кадров, пропуски, кэш',
      '«Следующая серия» на титрах, «Пропустить заставку» и «Пропустить титры» по главам',
      'Жесты мыши: удержание даёт 2×, колесо над громкостью, скоростью и полосой, правый щелчок сбрасывает',
      'Полный экран с автоскрытием, «картинка в картинке», мини-плеер',
      'Настройки для каждого файла: дорожки, задержки, скорость, поворот, петля A-B',
      'Слова в субтитрах: на паузе щелчок по слову копирует, открывает словарь или перевод',
    ],
  },
  {
    id: 'video', title: 'Видео', lead: 'Обработка идёт на видеокарте, кадры не копируются в память.',
    items: [
      'Кроп, резкость (шейдер CAS) и деинтерлейс на видеокарте',
      'Пресеты качества и алгоритмы увеличения',
      'ИИ-апскейл NVIDIA RTX и Intel, RTX Video HDR',
      'Устранение полос, тональная компрессия HDR, «Плавное движение»',
      'Свои шейдеры: файлы .glsl и .hook в папке shaders',
    ],
  },
  {
    id: 'zvuk', title: 'Звук', lead: 'Для ресивера, для наушников и для тихого вечера.',
    items: [
      'Передача объёмного звука на ресивер: AC3, E-AC3, DTS, DTS-HD, TrueHD',
      'Монопольный режим WASAPI',
      'Replay Gain и выравнивание громкости вместе с эквалайзером',
      'Громкость до 200 %',
      'Плавный переход между песнями от 1 до 12 секунд',
    ],
  },
  {
    id: 'muzyka', title: 'Музыка', lead: 'Свой экран для песен, а не видеоплеер с чёрным квадратом.',
    items: [
      'Обложка, визуализация (спектр, круг, осциллограф, волна) и синхронные тексты',
      'Тексты из .lrc рядом с треком, из тегов SYLT и USLT или из LRCLIB',
      'Песни, альбомы с шапкой и дисками, исполнители, жанры',
      'Собственный быстрый парсер тегов: MP3, FLAC, Ogg, Opus, M4A, WAV, AIFF, APE, WavPack',
    ],
  },
  {
    id: 'subtitry', title: 'Субтитры', lead: 'Видно, как будет выглядеть, ещё в настройках.',
    items: [
      'Плашка, размытие, отступ, вывод в чёрных полосах',
      'Вторые субтитры сверху',
      'Живой образец стиля в настройках',
      'Поиск в OpenSubtitles со своим ключом',
      'Moonbow запоминает пару «видео и файл субтитров»',
    ],
  },
  {
    id: 'mediateka', title: 'Медиатека', lead: 'Ваши папки, разложенные по полкам. Файлы не трогаются.',
    items: [
      'Главная: продолжить просмотр с «осталось N», следующая серия, недавние, частые папки',
      'Видео по папкам, сериалы по сезонам',
      'Сетевые потоки и радио, плейлисты, история',
      'Сетка или список с колонками, сортировка, поиск по названиям, исполнителям и альбомам',
      'Значки «Новое» и «Битый файл», полоса прогресса сканирования',
      'Постеры и описания из TMDB, если включить и ввести свой ключ',
    ],
  },
  {
    id: 'vid', title: 'Оформление', lead: 'Монохром, чтобы цвет оставался у фильма.',
    items: [
      'Тёмная, светлая и системная тема',
      'Пять тонов акцента: Белый, Жемчуг, Серебро, Сталь, Графит',
      'Скины «Чёрный OLED», «Высокий контраст», «Сепия» или свой JSON с перезагрузкой на лету',
      'Русский и английский интерфейс, переключается без перезапуска',
    ],
  },
  {
    id: 'sistema', title: 'Windows', lead: 'Ведёт себя как родное приложение, потому что оно родное.',
    items: [
      'Кнопки на превью в панели задач',
      'Медиаклавиши и карточка громкости Windows',
      'Значок в области уведомлений',
      'Ассоциации файлов, только по кнопке в настройках',
      'Редактор горячих клавиш с раскладкой «как в YouTube»',
    ],
  },
];

export type Key = { action: string; keys: string[][] };
export type KeyGroup = { title: string; keys: Key[] };
// Each inner array is one chord ("Ctrl", "Shift", "S"); alternatives are separate arrays.
export const keyGroups: KeyGroup[] = [
  {
    title: 'Воспроизведение',
    keys: [
      { action: 'Пауза и воспроизведение', keys: [['Space']] },
      { action: 'Стоп; Пробел продолжит с места остановки', keys: [['S']] },
      { action: 'Закрыть файл', keys: [['Ctrl', 'W']] },
      { action: 'Кадр вперёд и назад', keys: [['.'], [',']] },
      { action: 'Предыдущий и следующий файл', keys: [['P'], ['N']] },
      { action: 'Предыдущая и следующая глава', keys: [['Shift', 'PgUp'], ['Shift', 'PgDn']] },
      { action: 'С начала', keys: [['Home']] },
      { action: 'Повтор, перемешать, петля A-B', keys: [['L'], ['H'], ['Shift', 'L']] },
    ],
  },
  {
    title: 'Перемотка',
    keys: [
      { action: '10 секунд', keys: [['←'], ['→']] },
      { action: '3 секунды', keys: [['Shift', '←'], ['Shift', '→']] },
      { action: '1 минута', keys: [['Ctrl', '←'], ['Ctrl', '→']] },
      { action: '5 минут', keys: [['Ctrl', 'Shift', '←'], ['Ctrl', 'Shift', '→']] },
    ],
  },
  {
    title: 'Звук и скорость',
    keys: [
      { action: 'Громкость', keys: [['↑'], ['↓']] },
      { action: 'Без звука', keys: [['M']] },
      { action: 'Медленнее, быстрее, обычная скорость', keys: [['['], [']'], ['Backspace']] },
      { action: 'Следующая аудиодорожка', keys: [['A']] },
      { action: 'Задержка звука ±50 мс', keys: [['K'], ['J']] },
    ],
  },
  {
    title: 'Субтитры',
    keys: [
      { action: 'Следующие субтитры', keys: [['V']] },
      { action: 'Показать или скрыть', keys: [['Shift', 'V']] },
      { action: 'Задержка субтитров ±50 мс', keys: [['Shift', 'H'], ['Shift', 'G']] },
      { action: 'Слово в субтитрах на паузе', keys: [['W']] },
      { action: 'Добавить субтитры', keys: [['Ctrl', 'Shift', 'L']] },
    ],
  },
  {
    title: 'Окно',
    keys: [
      { action: 'Полный экран', keys: [['F'], ['F11'], ['Alt', 'Enter']] },
      { action: 'Выйти из полного экрана', keys: [['Esc']] },
      { action: 'Картинка в картинке', keys: [['Ctrl', 'Shift', 'P']] },
      { action: 'Снимок кадра', keys: [['Ctrl', 'Shift', 'S']] },
      { action: 'Запись потока', keys: [['Ctrl', 'Shift', 'R']] },
      { action: 'Информация о файле', keys: [['Ctrl', 'I']] },
      { action: 'Статистика поверх видео', keys: [['Ctrl', 'Shift', 'I']] },
    ],
  },
  {
    title: 'Медиатека',
    keys: [
      { action: 'Открыть файл', keys: [['Ctrl', 'O']] },
      { action: 'Открыть папку', keys: [['Ctrl', 'Shift', 'O']] },
      { action: 'Сетевой поток', keys: [['Ctrl', 'N']] },
      { action: 'Очередь', keys: [['Ctrl', 'L']] },
      { action: 'Боковая панель', keys: [['Ctrl', 'B']] },
      { action: 'Поиск', keys: [['Ctrl', 'F'], ['Ctrl', 'K']] },
      { action: 'Назад и вперёд', keys: [['Alt', '←'], ['Alt', '→']] },
      { action: 'Настройки', keys: [['Ctrl', 'P'], ['Ctrl', ',']] },
      { action: 'Все клавиши в самом плеере', keys: [['F1']] },
      { action: 'Выход', keys: [['Ctrl', 'Q']] },
    ],
  },
];

export const dataFiles = [
  ['settings.json', 'настройки и переназначенные клавиши'],
  ['library.json', 'папки медиатеки и результаты сканирования'],
  ['history.json', 'история и позиции просмотра (отключается)'],
  ['playlists.json', 'плейлисты, закладки, избранное'],
  ['file-settings.json', 'настройки отдельных файлов'],
  ['online\\', 'кэш LRCLIB, OpenSubtitles и TMDB, если они включены'],
];

export const credits = [
  { what: 'Кадр «Вечер у воды»', who: 'Christopher Sardegna', href: 'https://unsplash.com/photos/CMOa3H1SXG0' },
  { what: '«Тихий берег», серии 1 и 2', who: 'Jake Melara, Rosan Harmens', href: 'https://unsplash.com/photos/bA68bHUS9mA' },
  { what: '«Тихий берег», серии 3 и 4', who: 'Ryan Lum, Robin Röcker', href: 'https://unsplash.com/photos/1ak3Z7ZmtQA' },
  { what: 'Обложка «Огни»', who: 'Maria Carrasco', href: 'https://unsplash.com/photos/vwY2D2Wr4ME' },
  { what: 'Другие постеры', who: 'Joshua Earle, Stefan Ringler, Kevin Young, Hartmut Tobies, Israel Sundseth, Guillaume, Glen Carrie, Joshua Sortino, Rula Sibai, Nelly Volkovich', href: 'https://unsplash.com/' },
];
