Lumen — бета-версия для тестирования
====================================

Спасибо, что помогаете. Это ранняя сборка: в ней могут быть ошибки, ради их поиска она и нужна.

КАК УСТАНОВИТЬ
1. Запустите Lumen-...-setup.exe. Права администратора не нужны. Windows может предупредить
   «Система Windows защитила ваш компьютер»: сборка пока не подписана. Нажмите «Подробнее» →
   «Выполнить в любом случае».
2. Установщик положит Lumen в %LOCALAPPDATA%\Programs\Lumen, добавит его в меню «Пуск» и в
   «Открыть с помощью» для видео, музыки и плейлистов.
3. Оставьте галочку «Сделать Lumen плеером по умолчанию»: откроются параметры Windows, выберите там
   Lumen. Windows не даёт программам делать это самим, поэтому последний шаг за вами.
   Позже то же самое: в Lumen Настройки → Общие → «Сделать Lumen плеером по умолчанию».
4. Пользуйтесь как обычным плеером: смотрите свои видео, слушайте музыку, добавьте папки в медиатеку.
Без установки: распакуйте .zip в любую папку и запустите Lumen.exe.

ЕСЛИ ЧТО-ТО ПОШЛО НЕ ТАК
1. Откройте Настройки → Общие → Диагностика → «Журналы и отчёты о сбоях» → «Открыть папку».
   После сбоя Lumen сам предложит «Показать отчёт» при следующем запуске.
2. Пришлите все файлы из этой папки тому, от кого получили сборку, и напишите в двух словах:
   что вы делали, что ожидали увидеть и что случилось вместо этого. Скриншот тоже очень помогает.
3. Если неправильно играет конкретный файл, включите там же «Подробный журнал воспроизведения»,
   откройте этот файл ещё раз и пришлите mpv.log из той же папки. Потом журнал можно выключить.

ЧТО ЛЕЖИТ В ЭТИХ ФАЙЛАХ
- lumen.log и lumen.1.log: журнал этого и прошлого запуска. Версия Windows, видеокарта, сообщения
  mpv о проблемах воспроизведения, имена файлов, которые не открылись.
- crash-*.txt и crash-*.dmp: отчёт о сбое. В .dmp только состояние программы в момент сбоя
  (стеки потоков и список модулей), без содержимого ваших видео и музыки.
Lumen ничего не отправляет сам: файлы остаются на вашем компьютере, пока вы их не пришлёте.

КАК ОБНОВИТЬ
Сам Lumen пока не обновляется: новую сборку вы получите файлом.
- Установщик: просто запустите новый setup.exe, он поставит Lumen поверх старого.
- Архив: закройте Lumen и распакуйте новый архив поверх старой папки с заменой файлов.
Настройки и медиатека хранятся отдельно, в %APPDATA%\Lumen, и при обновлении не теряются.
Когда сообщаете о проблеме, назовите имя файла сборки (вида Lumen-0.1.0-beta-1a2b3c4): по нему
видно, какая это сборка.

КАК ВЕРНУТЬ ВСЁ КАК БЫЛО
- Другой плеер по умолчанию: Параметры Windows → Приложения → Приложения по умолчанию, выберите
  формат (например .mp4) и прежний плеер. Внизу там же есть «Сбросить» для всех форматов.
- Удалить Lumen: Параметры Windows → Приложения → Установленные приложения → Lumen → Удалить.
  Lumen уберёт себя из «Открыть с помощью» и спросит, удалять ли настройки и медиатеку.
- Версия из архива: удалите её папку и папку %APPDATA%\Lumen.

Lumen распространяется по лицензии GPL-3.0-or-later. Воспроизведение работает на libmpv (mpv.io).


Lumen — beta for testers (English)
----------------------------------
Run Lumen-...-setup.exe (no admin rights; or unzip the .zip and run Lumen.exe). The build is not
signed yet: on the SmartScreen warning choose "More info" → "Run anyway". Keep "Make Lumen the
default player" ticked and pick Lumen in Windows Settings: Windows lets only you change defaults.
If something goes wrong: Settings → General → Diagnostics → "Logs and crash reports" → "Open
folder", and send every file from there together with a few words on what you did and what
happened. For a file that plays wrong, turn on "Detailed playback log", play it again and send
mpv.log too. Nothing is sent automatically; crash dumps hold thread stacks and the module list,
not your media. Settings and the library live in %APPDATA%\Lumen.
To update: run the new setup (or unzip over the old folder); settings and the library stay.
To undo: pick another player in Settings → Apps → Default apps, or uninstall Lumen in Settings →
Apps → Installed apps: it removes its file types and asks whether to delete its data.
When reporting, name the build you run (like Lumen-0.1.0-beta-1a2b3c4).
