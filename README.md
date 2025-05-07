<h1 align="center">
    <img src="logo.ico" alt="Trayy logo" width="80"/>
    <div>
    Trayy
</h1>

<p align="center">
Ever wished you could tell some apps to chill in the system tray? </br>Say hello to Trayy, your desktop's new bouncer!
</p>

## 🎯 Features

- **Send to Tray**: Send your favorite apps to the system tray for a clutter-free workspace!
- **Hide and Seek Champion**: Enjoy the power to completely hide applications from the taskbar, keeping your desktop neat and tidy.
- **App Support**: Ideal for Progressive Web Apps (PWAs) and (most) apps that didn't get the memo about system tray support such as Thunderbird.
- **Compatible with Windows**: Tested on Windows 10 and 11 with support for toast notifications.
- **Light as a Feather**: Fully portable and extremely lightweight.

## 🚀 Getting Started

1. Kick things off by downloading `Trayy.zip` and unzipping it to reveal the magic inside!
2. Make sure `Trayy.exe` and `hook.dll` are hanging out in the same folder. Now, Windows Defender might raise a false positive but fear not! Trayy is harmless and also open source ([VirusTotal report](https://www.virustotal.com/gui/file/688011ba8305871139bac0b7da0da7f2e56370e65f9909bea2350723b9db2822/detection)). Run the app to get the party started.
3. Time to pick your superpowers:
   - **Send to Tray also when Closed**: Even if you hit the X button, your app will just chill in the tray.
   - **Do not show on Taskbar**: Your app will become the ultimate hide-and-seek champion, staying off the taskbar completely.
4. Now, list out your favorite applications (case-sensitive).
   - Trayy will keep an eye out for any Windows process names that match your entries.
   - For Web Apps, Trayy will look for tab names that contain your specified string.
   - Got an app (like Thunderbird) that doesn’t play nice with Trayy's standard functionality? Just add an asterisk `*` to the end of its name (e.g., `Thunderbird*`). This tells Trayy to use a special method to detect minimize or close actions by looking for clicks on the top-right of the titlebar, so even tricky programs can be tucked away smoothly!
   - **Heads up:** Universal Windows Platform (UWP) apps (like those from the Microsoft Store) like to do their own thing and aren’t supported by Trayy.
5. Hit Save and BAM! Depending on your settings, your chosen applications will now be tucked away neatly in the system tray.
6. Click on a tray icon to bring its application into the spotlight. If it's already in focus, it'll sneak back into the tray. This way, you can quickly peek at your apps without breaking your workflow!

<p align="center">
  <img src="demo.png" alt="GUI demo" height="375" width="auto"/>
  <img src="demo.gif" alt="Video demo" width="auto" height="375" autoplay/>
</p>

**Pro Move**: Let Trayy join your startup squad! Add Trayy's shortcut to your `shell:startup` folder, along with all your other favorite apps you want to keep tucked away in the system tray. For WebApps, you can ask your browser to add their shortcuts to your Desktop. Then, simply move those shortcuts into the startup folder. Trayy will chill for a bit, letting the startup process finish, then swoop in to tidy everything up! It's a game changer for messaging and productivity apps!

## 🙏 Acknowledgements

This project is inspired by RBTray.
