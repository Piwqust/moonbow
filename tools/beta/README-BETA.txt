Moonbow — бета-версия для тестирования
======================================

Спасибо, что помогаете. Это ранняя сборка: в ней могут быть ошибки, ради их поиска она и нужна.

ЕСЛИ У ВАС СТОЯЛ LUMEN
Moonbow — это тот же плеер под новым именем. При первом запуске он сам забирает настройки,
историю, медиатеку и сохранённые ключи из Lumen; сам Lumen остаётся как был. Когда убедитесь, что
всё на месте, Lumen можно удалить: Параметры Windows → Приложения → Установленные приложения.
Тогда снова выберите Moonbow плеером по умолчанию (шаг 3 ниже), если файлы открывались в Lumen.

КАК УСТАНОВИТЬ
1. Запустите Moonbow-...-setup.exe. Права администратора не нужны. Windows может предупредить
   «Система Windows защитила ваш компьютер»: сборка пока не подписана. Нажмите «Подробнее» →
   «Выполнить в любом случае».
2. Нажмите «Установить». Moonbow встанет в %LOCALAPPDATA%\Programs\Moonbow, появится в меню «Пуск» и в
   «Открыть с помощью» для видео, музыки и плейлистов. Другая папка: «Параметры установки».
3. Видео сразу начнут открываться в Moonbow, музыка останется в прежней программе. Не хотите этого:
   выключите «Открывать видео в Moonbow по умолчанию» в «Параметрах установки». Если Windows не
   позволит, откроются её «Приложения по умолчанию», выберите там Moonbow. То же делает кнопка в
   Moonbow: Настройки → Общие → «Открывать видео в Moonbow».
4. Пользуйтесь как обычным плеером: смотрите свои видео, слушайте музыку, добавьте папки в медиатеку.
Без установки: распакуйте .zip в любую папку и запустите Moonbow.exe.

ЕСЛИ ЧТО-ТО ПОШЛО НЕ ТАК
1. Откройте Настройки → Общие → Диагностика → «Журналы и отчёты о сбоях» → «Открыть папку».
   После сбоя Moonbow сам предложит «Показать отчёт» при следующем запуске.
2. Пришлите все файлы из этой папки тому, от кого получили сборку, и напишите в двух словах:
   что вы делали, что ожидали увидеть и что случилось вместо этого. Скриншот тоже очень помогает.
3. Если неправильно играет конкретный файл, включите там же «Подробный журнал воспроизведения»,
   откройте этот файл ещё раз и пришлите mpv.log из той же папки. Потом журнал можно выключить.

ЧТО ЛЕЖИТ В ЭТИХ ФАЙЛАХ
- moonbow.log и moonbow.1.log: журнал этого и прошлого запуска. Версия Windows, видеокарта, сообщения
  mpv о проблемах воспроизведения, имена файлов, которые не открылись.
- crash-*.txt и crash-*.dmp: отчёт о сбое. В .dmp только состояние программы в момент сбоя
  (стеки потоков и список модулей), без содержимого ваших видео и музыки.
Moonbow ничего не отправляет сам: файлы остаются на вашем компьютере, пока вы их не пришлёте.

КАК ОБНОВИТЬ ИЛИ ВЕРНУТЬСЯ
Сам Moonbow пока не обновляется: новую сборку вы получите файлом.
- Запустите новый setup.exe: он покажет, что изменилось, и обновит Moonbow. Открытый Moonbow закроется
  сам, место, где вы остановились, сохранится. После обновления Moonbow один раз покажет «Что нового».
- Прежняя версия остаётся на компьютере (хранятся две прошлые). Вернуться к ней: Moonbow → меню
  логотипа → «О программе» → «Версии на этом компьютере» → «Вернуться на неё». Или запустите
  setup.exe той версии: он сам поймёт, что это возврат.
- Архив: закройте Moonbow и распакуйте новый архив поверх старой папки с заменой файлов.
Настройки и медиатека хранятся отдельно, в %APPDATA%\Moonbow, и при обновлении не теряются.
Когда сообщаете о проблеме, назовите имя файла сборки (вида Moonbow-0.1.0-beta-1a2b3c4): по нему
видно, какая это сборка.

КАК ВЕРНУТЬ ВСЁ КАК БЫЛО
- Другой плеер по умолчанию: Параметры Windows → Приложения → Приложения по умолчанию, выберите
  формат (например .mp4) и прежний плеер. Внизу там же есть «Сбросить» для всех форматов.
- Удалить Moonbow: Параметры Windows → Приложения → Установленные приложения → Moonbow → Удалить.
  Moonbow уберёт себя из «Открыть с помощью», вернёт для видео те программы, что открывали
  их до Moonbow. Настройки и медиатека останутся, если не отметить «Удалить также настройки».
- Версия из архива: удалите её папку и папку %APPDATA%\Moonbow.

Moonbow распространяется по лицензии GPL-3.0-or-later. Воспроизведение работает на libmpv (mpv.io).


Moonbow — beta for testers (English)
------------------------------------
Had Lumen? Moonbow is the same player under a new name: the first start takes Lumen's settings,
history, library and saved keys, and leaves Lumen as it was. Uninstall Lumen once all is there.
Run Moonbow-...-setup.exe and press Install (no admin rights; or unzip the .zip and run Moonbow.exe).
The build is not signed yet: on the SmartScreen warning choose "More info" → "Run anyway". Videos
then open in Moonbow, music stays as it was ("Install options" turns that off; Windows' Default apps
opens if it refuses).
If something goes wrong: Settings → General → Diagnostics → "Logs and crash reports" → "Open
folder", and send every file from there together with a few words on what you did and what
happened. For a file that plays wrong, turn on "Detailed playback log", play it again and send
mpv.log too. Nothing is sent automatically; crash dumps hold thread stacks and the module list,
not your media. Settings and the library live in %APPDATA%\Moonbow.
To update: run the new setup (or unzip over the old folder); settings and the library stay. The
previous version is kept: Moonbow's logo menu → About Moonbow → Versions on this computer takes you
back, and so does running that version's setup.
To undo: pick another player in Settings → Apps → Default apps, or uninstall Moonbow in Settings →
Apps → Installed apps: it removes its file types, gives each type back the app it had before
Moonbow, and keeps its data unless you tick "Also delete settings".
When reporting, name the build you run (like Moonbow-0.1.0-beta-1a2b3c4).
