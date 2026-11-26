# UFO 50 Android Online - the builder: a window for the quick build. Double-click builder.bat.
#
# It runs build.ps1 for each thing you tick (the Android app, the Windows version, the plain game
# for Windows) and shows its progress as steps. Everything the build does is build.ps1's: this is
# only the window around it, so the two can't disagree. Like build.ps1 it needs Windows 10 or 11
# (PowerShell and WPF come with them), no admin rights, and UFO 50 from Steam.
#
# One version = one set of mods and features (everyone on a version plays together), so there is
# nothing here to pick mods with: that is build.ps1's $Pins, changed with a new version.
#
# For tests (not for players):
#   -GamePath <dir>      start with this folder instead of looking for the game
#   -Test <targets>      tick these (android,windows,plain), press Build, close at the end
#   -Shots <dir>         save a picture of the window at each screen (needs -Test)
#   -BuildScript <ps1>   run this instead of build.ps1 (tests/gui_replay.ps1 plays a recorded log)
[CmdletBinding(PositionalBinding = $false)]
param([string]$GamePath = "", [string]$Test = "", [string]$Shots = "", [string]$BuildScript = "")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Windows.Forms

$Root = $PSScriptRoot
if (-not $BuildScript) { $BuildScript = Join-Path $Root "build.ps1" }
$Version = (Get-Content -LiteralPath (Join-Path $Root "VERSION") -TotalCount 1).Trim()
# the game version and data.win checksum this version is made for: build.ps1's own pins
$pins = Get-Content -Raw -LiteralPath (Join-Path $Root "build.ps1")
$GameVersion = [regex]::Match($pins, 'GameVersion\s*=\s*"([^"]+)"').Groups[1].Value
$GameSha = [regex]::Match($pins, 'GameSha\s*=\s*"([0-9a-f]+)"').Groups[1].Value

# build.ps1 runs as a child process; its lines are collected on .NET's threads and read by the
# window's timer (a PowerShell script block can't run on those threads)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
public class BuildRun
{
    Process p;
    ConcurrentQueue<string> lines = new ConcurrentQueue<string>();
    public void Start(string file, string args, string dir)
    {
        var si = new ProcessStartInfo(file, args);
        si.UseShellExecute = false; si.CreateNoWindow = true; si.WorkingDirectory = dir;
        si.RedirectStandardOutput = true; si.RedirectStandardError = true;
        p = new Process(); p.StartInfo = si;
        p.OutputDataReceived += (s, e) => { if (e.Data != null) lines.Enqueue(e.Data); };
        p.ErrorDataReceived += (s, e) => { if (e.Data != null) lines.Enqueue(e.Data); };
        p.Start(); p.BeginOutputReadLine(); p.BeginErrorReadLine();
    }
    public string[] Take()
    {
        var all = new System.Collections.Generic.List<string>(); string l;
        while (lines.TryDequeue(out l)) all.Add(l);
        return all.ToArray();
    }
    public bool Finished() { if (!p.HasExited) return false; p.WaitForExit(); return true; }
    public int Code() { return p.ExitCode; }
    public void Stop()
    {
        try { Process.Start(new ProcessStartInfo("taskkill", "/PID " + p.Id + " /T /F") { CreateNoWindow = true, UseShellExecute = false }).WaitForExit(); } catch { }
    }
}
'@

# ---- the window -------------------------------------------------------------------------------
# Colours: the overlay's (docs: style guide). One green button per screen, navy for the rest,
# orange only for something that needs you.
[xml]$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="UFO 50 Android Online Builder" Width="820" SizeToContent="Height" ResizeMode="CanMinimize"
        WindowStartupLocation="CenterScreen" Background="#000000" FontFamily="Segoe UI" FontSize="13">
  <Window.Resources>
    <!-- every colour here is one of UFO 50's (the palette in scripts/make_icon.py, read from the game's textures) -->
    <SolidColorBrush x:Key="Text" Color="#FFFFFF"/>
    <SolidColorBrush x:Key="Muted" Color="#C8C8C8"/>
    <Style TargetType="TextBlock"><Setter Property="Foreground" Value="{StaticResource Text}"/><Setter Property="TextWrapping" Value="Wrap"/></Style>
    <Style x:Key="Label" TargetType="TextBlock"><Setter Property="Foreground" Value="{StaticResource Muted}"/><Setter Property="FontSize" Value="12"/><Setter Property="Margin" Value="0,12,0,6"/></Style>
    <Style x:Key="Hint" TargetType="TextBlock"><Setter Property="Foreground" Value="{StaticResource Muted}"/><Setter Property="FontSize" Value="12"/><Setter Property="TextWrapping" Value="Wrap"/></Style>
    <Style TargetType="Button">
      <Setter Property="Foreground" Value="{StaticResource Text}"/><Setter Property="Background" Value="#202040"/>
      <Setter Property="BorderBrush" Value="#4430BA"/><Setter Property="Padding" Value="14,7"/><Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">
        <Border x:Name="b" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="1" CornerRadius="6" Padding="{TemplateBinding Padding}">
          <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border>
        <ControlTemplate.Triggers>
          <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="b" Property="BorderBrush" Value="#C8C8C8"/></Trigger>
          <Trigger Property="IsEnabled" Value="False"><Setter TargetName="b" Property="Opacity" Value="0.4"/></Trigger>
        </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
    </Style>
    <Style x:Key="Primary" TargetType="Button" BasedOn="{StaticResource {x:Type Button}}">
      <Setter Property="Background" Value="#008456"/><Setter Property="BorderBrush" Value="#008456"/><Setter Property="FontWeight" Value="SemiBold"/><Setter Property="Padding" Value="22,9"/>
    </Style>
    <Style x:Key="Link" TargetType="Button" BasedOn="{StaticResource {x:Type Button}}">
      <Setter Property="Background" Value="Transparent"/><Setter Property="BorderBrush" Value="Transparent"/><Setter Property="Padding" Value="0"/><Setter Property="Foreground" Value="#27BADB"/>
    </Style>
    <Style TargetType="CheckBox">
      <Setter Property="Foreground" Value="{StaticResource Text}"/><Setter Property="Cursor" Value="Hand"/><Setter Property="Margin" Value="0,0,0,7"/>
      <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="CheckBox">
        <Grid Background="Transparent"><Grid.ColumnDefinitions><ColumnDefinition Width="28"/><ColumnDefinition/></Grid.ColumnDefinitions>
          <Border x:Name="box" Width="18" Height="18" VerticalAlignment="Top" Margin="0,1,0,0" CornerRadius="3" Background="#202040" BorderBrush="#6F6F6F" BorderThickness="1.5">
            <TextBlock x:Name="tick" Text="&#x2713;" FontWeight="Bold" FontSize="13" Foreground="White" HorizontalAlignment="Center" VerticalAlignment="Center" Visibility="Hidden" Margin="0,-1,0,0"/></Border>
          <ContentPresenter Grid.Column="1"/></Grid>
        <ControlTemplate.Triggers>
          <Trigger Property="IsChecked" Value="True"><Setter TargetName="tick" Property="Visibility" Value="Visible"/><Setter TargetName="box" Property="BorderBrush" Value="#FFFFFF"/></Trigger>
          <Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.4"/></Trigger>
        </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
    </Style>
  </Window.Resources>
  <ScrollViewer VerticalScrollBarVisibility="Auto">
  <Border x:Name="Shot" Background="#000000" Padding="28,16,28,18">
  <StackPanel>
    <DockPanel>
      <TextBlock DockPanel.Dock="Right" x:Name="VersionText" Foreground="{StaticResource Muted}" VerticalAlignment="Bottom" Margin="0,0,0,3"/>
      <TextBlock Text="UFO 50 Android Online" FontSize="22" FontWeight="SemiBold"/>
    </DockPanel>
    <DockPanel Margin="0,2,0,0">
      <Button x:Name="GitHubBtn" DockPanel.Dock="Right" Style="{StaticResource Link}" FontSize="12" Content="github.com/Patrick-Working/UFO50AndroidOnline"/>
      <TextBlock x:Name="Subtitle" Style="{StaticResource Hint}" FontSize="13"/>
    </DockPanel>
    <Border x:Name="CreditsBox" CornerRadius="6" Padding="12,8" Background="#202040" Margin="0,10,0,0">
      <StackPanel>
        <TextBlock Text="BUILT WITH" Foreground="{StaticResource Muted}" FontSize="12" Margin="0,0,0,4"/>
        <Grid x:Name="Credits"><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions></Grid>
        <TextBlock Style="{StaticResource Hint}" FontStyle="Italic" Margin="0,6,0,0" Text="Downloaded from their authors' pages when you build, each checked against a fixed checksum: none of them is stored in this project."/>
      </StackPanel>
    </Border>

    <!-- 1. what to build -->
    <StackPanel x:Name="PageSetup">
      <TextBlock Text="UFO 50 INSTALL DIRECTORY" Style="{StaticResource Label}"/>
      <Border x:Name="GameBox" CornerRadius="6" Padding="12,10" Background="#202040">
        <DockPanel>
          <Button x:Name="ChangeGame" DockPanel.Dock="Right" Content="Change..." VerticalAlignment="Center" Margin="12,0,0,0"/>
          <StackPanel>
            <TextBlock x:Name="GameTitle" FontWeight="SemiBold"/>
            <TextBlock x:Name="GameDetail" Style="{StaticResource Hint}" Margin="0,2,0,0"/>
          </StackPanel>
        </DockPanel>
      </Border>

      <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="28"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
      <StackPanel Grid.Column="0">
      <TextBlock Text="WHAT TO BUILD" Style="{StaticResource Label}"/>
      <CheckBox x:Name="DoAndroid" IsChecked="True"><StackPanel>
        <TextBlock Text="Android App" FontWeight="SemiBold"/>
        <TextBlock Style="{StaticResource Hint}" Text="UFO 50 with touch controls, online play, save states and rewind. An app file (.apk) to put on your phone."/></StackPanel></CheckBox>
      <CheckBox x:Name="DoWindows" IsChecked="True"><StackPanel>
        <TextBlock Text="Windows (UFO Online)" FontWeight="SemiBold"/>
        <TextBlock Style="{StaticResource Hint}" Text="Cross-play with Android online + save states and rewind. Removes Steam dependency. Win7 - Win11 compatible."/></StackPanel></CheckBox>
      <CheckBox x:Name="DoPlain"><StackPanel>
        <TextBlock Text="Windows (UFO Vanilla)" FontWeight="SemiBold"/>
        <TextBlock Style="{StaticResource Hint}" Text="A copy of your UFO 50, running without Steam on Windows 7, 10 and 11. No mods or online play."/></StackPanel></CheckBox>
      </StackPanel>
      <StackPanel Grid.Column="2">
      <TextBlock Text="ON YOUR PHONE" Style="{StaticResource Label}"/>
      <CheckBox x:Name="DoInstall"><StackPanel>
        <TextBlock Text="Install the App on My Phone When It's Done (requires USB Connection)."/>
        <TextBlock x:Name="PhoneText" Style="{StaticResource Hint}"/></StackPanel></CheckBox>

      <Border CornerRadius="6" Padding="12,8" Background="#202040" Margin="0,4,0,0">
        <TextBlock x:Name="AboutText" Style="{StaticResource Hint}"/>
      </Border>
      </StackPanel>
      </Grid>
      <TextBlock x:Name="SetupError" Foreground="#FEB854" Margin="0,12,0,0" Visibility="Collapsed"/>
      <DockPanel Margin="0,8,0,0">
        <Button x:Name="BuildBtn" DockPanel.Dock="Right" HorizontalAlignment="Right" Style="{StaticResource Primary}" Content="Build"/>
      </DockPanel>
    </StackPanel>

    <!-- 2. building, 3. done or stopped -->
    <StackPanel x:Name="PageRun" Visibility="Collapsed">
      <Border x:Name="Banner" CornerRadius="6" Padding="12,10" Margin="0,18,0,0" Visibility="Collapsed">
        <StackPanel><TextBlock x:Name="BannerTitle" FontWeight="SemiBold" Foreground="White"/>
          <TextBlock x:Name="BannerText" Foreground="White" Margin="0,3,0,0"/></StackPanel>
      </Border>
      <Grid Margin="0,18,0,0" Height="6"><Border Background="#202040" CornerRadius="3"/>
        <Border x:Name="Bar" Background="#008456" CornerRadius="3" HorizontalAlignment="Left" Width="0"/></Grid>
      <StackPanel x:Name="Steps" Margin="0,6,0,0"/>
      <StackPanel x:Name="Results"/>
      <TextBox x:Name="Log" Visibility="Collapsed" Height="200" Margin="0,14,0,0" IsReadOnly="True" FontFamily="Consolas" FontSize="11"
               Background="#0B0606" Foreground="#C8C8C8" BorderBrush="#202040" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Auto" TextWrapping="NoWrap"/>
      <DockPanel Margin="0,18,0,0">
        <StackPanel DockPanel.Dock="Right" Orientation="Horizontal">
          <Button x:Name="StopBtn" Content="Stop" Margin="10,0,0,0"/>
          <Button x:Name="AgainBtn" Content="Build again" Margin="10,0,0,0" Visibility="Collapsed"/>
          <Button x:Name="CloseBtn" Style="{StaticResource Primary}" Content="Close" Margin="10,0,0,0" Visibility="Collapsed"/>
        </StackPanel>
        <StackPanel Orientation="Horizontal" VerticalAlignment="Center">
          <Button x:Name="LogBtn" Style="{StaticResource Link}" Content="Show details"/>
          <Button x:Name="CopyBtn" Style="{StaticResource Link}" Content="Copy details" Margin="16,0,0,0" Visibility="Collapsed"/>
        </StackPanel>
      </DockPanel>
    </StackPanel>
  </StackPanel>
  </Border>
  </ScrollViewer>
</Window>
'@
$win = [Windows.Markup.XamlReader]::Load((New-Object System.Xml.XmlNodeReader $xaml))
$ui = @{}
$xaml.SelectNodes("//*[@*[local-name()='Name']]") | ForEach-Object { $n = $_.GetAttribute("Name", "http://schemas.microsoft.com/winfx/2006/xaml"); $ui[$n] = $win.FindName($n) }

function Brush($hex) { New-Object Windows.Media.SolidColorBrush ([Windows.Media.ColorConverter]::ConvertFromString($hex)) }
$Green = "#63B31D"; $Orange = "#FE7000"; $Muted = "#C8C8C8"; $Blue = "#27BADB"
$Banner = "#CA4A00"   # the darker orange, so white text on it stays readable

$ui.VersionText.Text = "v$Version"
# who made what this is built from; the versions are build.ps1's pins, the ones a build downloads
function Pin($re) { [regex]::Match($pins, $re).Groups[1].Value }
$extra = @{}; foreach ($m in [regex]::Matches($pins, 'Name = "([^"]+)";[^\n]*?-(\d[\d.]*)\.zip"')) { $extra[$m.Groups[1].Value] = $m.Groups[2].Value }
$CreditList = @(
    @("UFO 50", "", "Mossmouth", "https://50games.fun"),
    @("PFO 50", (Pin 'pfo50-([\d.]+)\.zip'), "Blaizer: online multiplayer (MIT)", "https://gamebanana.com/mods/648717"),
    @("MultiMadnessMod", (Pin 'multimadnessmod-([\d.]+)\.zip'), "Jonesey13: 4+ players", "https://gamebanana.com/mods/605308"),
    @("Ultimate Pingolf Roster", $extra["Ultimate Pingolf Roster"], "Bubba256: 12 golfers", "https://gamebanana.com/mods/679074"),
    @("Star Waspir 2P", $extra["Star Waspir 2P"], "ToastGhost26: a second pilot", "https://gamebanana.com/mods/698092"),
    @("Hyper Contender+", $extra["Hyper Contender+"], "greenhead352: 4 contenders", "https://gamebanana.com/mods/604987"),
    @("UFO 50 Mod Loader", (Pin 'download/v([\d.]+)/UFO-50'), "Phil Macrocheira (MIT)", "https://github.com/phil-macrocheira/UFO-50-MOD-LOADER"),
    @("UndertaleModTool", (Pin 'download/([\d.]+)/UTMT'), "Underminers (GPL-3.0)", "https://github.com/UnderminersTeam/UndertaleModTool"),
    @("GameMaker", "", "YoYo Games: Android runner", "https://gamemaker.io"),
    @("ENet", "", "Lee Salzman: networking (MIT)", "https://github.com/lsalzman/enet"),
    @("api-ms-win-core-path-HACK", "", "nalexandru: Windows 7 (LGPL-2.1)", "https://github.com/nalexandru/api-ms-win-core-path-HACK"),
    @("Win7 patcher for GM", "", "YellowAfterlife: Windows 7", "https://yal.cc/gm2024-win7/"),
    @("UFO50AndroidUnofficial", "", "Skyline969", "https://github.com/Skyline969/UFO50AndroidUnofficial"))
$row = 0
foreach ($c in $CreditList) {
    $r = [math]::Floor($row / 2); $col = ($row % 2) * 2
    if ($col -eq 0) { $ui.Credits.RowDefinitions.Add((New-Object Windows.Controls.RowDefinition)) }
    $b = New-Object Windows.Controls.Button; $b.Style = $win.FindResource("Link"); $b.FontSize = 12; $b.HorizontalAlignment = "Left"; $b.Tag = $c[3]
    $b.Content = if ($c[1]) { "$($c[0]) $($c[1])" } else { $c[0] }
    $b.Add_Click({ param($s) Start-Process $s.Tag })
    $b.Margin = "0,1,12,1"; [Windows.Controls.Grid]::SetRow($b, $r); [Windows.Controls.Grid]::SetColumn($b, $col)
    $t = New-Object Windows.Controls.TextBlock; $t.Style = $win.FindResource("Hint"); $t.Text = $c[2]; $t.VerticalAlignment = "Center"
    $t.Margin = "0,0,16,0"; [Windows.Controls.Grid]::SetRow($t, $r); [Windows.Controls.Grid]::SetColumn($t, $col + 1)
    $ui.Credits.Children.Add($b) | Out-Null; $ui.Credits.Children.Add($t) | Out-Null
    $row++
}
$ui.Subtitle.Text = "Script to build UFO 50 Android Online."
$ui.AboutText.Text = "Everyone needs the same version to play together online (this build: $Version). The first build downloads " +
    "about 400 MB and takes about 10 minutes. Your Steam folder / game saves aren't changed; no admin rights needed."

# ---- your game ----------------------------------------------------------------------------------
function Find-Game {
    $libs = @()
    try { $libs += (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction Stop).SteamPath } catch {}
    $libs += "C:\Program Files (x86)\Steam"
    foreach ($s in @($libs)) {
        $vdf = Join-Path $s "steamapps\libraryfolders.vdf"
        if (Test-Path -LiteralPath $vdf) { $libs += (Select-String -LiteralPath $vdf -Pattern '"path"\s+"([^"]+)"' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' } }
    }
    foreach ($l in $libs) { $p = Join-Path $l "steamapps\common\UFO 50"; if (Test-Path -LiteralPath (Join-Path $p "data.win")) { return $p } }
    return ""
}
$script:Game = ""; $script:GameOk = $false
function Show-Game($path) {
    $script:Game = $path; $script:GameOk = $false
    $ui.GameBox.BorderThickness = 1
    if (-not $path -or -not (Test-Path -LiteralPath (Join-Path $path "data.win"))) {
        $ui.GameTitle.Text = "UFO 50 Wasn't Found"
        $ui.GameDetail.Text = if ($path) { "There's no UFO 50 in $path. Choose its install directory (the folder with data.win)." } else { "Install it from Steam, or choose its install directory (the folder with data.win)." }
        $ui.GameBox.BorderBrush = Brush $Orange; $ui.ChangeGame.Content = "Choose folder..."
    }
    elseif ((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $path "data.win")).Hash.ToLower() -ne $GameSha) {
        $ui.GameTitle.Text = "This UFO 50 Isn't Version $GameVersion"
        $ui.GameDetail.Text = "v$Version is made for UFO 50 $GameVersion. Update the game in Steam, then choose it again. ($path)"
        $ui.GameBox.BorderBrush = Brush $Orange; $ui.ChangeGame.Content = "Choose again..."
    }
    else {
        $script:GameOk = $true
        $ui.GameTitle.Text = "$([char]0x2713)  UFO 50 $GameVersion"
        $ui.GameDetail.Text = $path
        $ui.GameBox.BorderBrush = Brush "#202040"; $ui.ChangeGame.Content = "Change..."
    }
    Update-Setup
}

# ---- your phone (adb, if it's there) ------------------------------------------------------------
$script:Adb = (Get-Command adb -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $script:Adb) { $a = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"; if (Test-Path $a) { $script:Adb = $a } }
# The app goes on one device: a phone on USB first (an emulator's name has a ':' or starts with
# "emulator-"). ANDROID_SERIAL tells adb which, build.ps1's -Install included, which otherwise
# stops when more than one is connected.
$script:Phones = 0; $script:Serial = ""
function Check-Phone {
    $script:Phones = 0; $script:Serial = ""
    if ($script:Adb) {
        try {
            $list = @((& $script:Adb devices 2>$null) | Where-Object { $_ -match "`tdevice$" } | ForEach-Object { ($_ -split "`t")[0] })
            $script:Phones = $list.Count
            $usb = @($list | Where-Object { $_ -notmatch ":" -and $_ -notmatch "^emulator-" })
            $script:Serial = if ($usb.Count) { $usb[0] } elseif ($list.Count) { $list[0] } else { "" }
        } catch {}
    }
    if ($script:Serial) { $env:ANDROID_SERIAL = $script:Serial }
    Update-Setup
}

function Update-Setup {
    $winTicked = $ui.DoWindows.IsChecked -or $ui.DoPlain.IsChecked
    if (-not $script:Adb) {
        $ui.PhoneText.Text = "Needs Android's adb tool, which isn't on this PC. Without it, copy the app to your phone yourself."
        $ui.DoInstall.IsEnabled = $false; $ui.DoInstall.IsChecked = $false
    } elseif ($script:Phones -eq 0) {
        $ui.PhoneText.Text = "No phone found. Connect it with a USB cable and USB debugging on, then open this again."
        $ui.DoInstall.IsEnabled = $false; $ui.DoInstall.IsChecked = $false
    } else {
        $ui.PhoneText.Text = if ($script:Phones -eq 1) { "Phone found." } else { "$($script:Phones) devices found: it goes on the one connected by USB." }
        $ui.DoInstall.IsEnabled = [bool]$ui.DoAndroid.IsChecked
        if (-not $ui.DoAndroid.IsChecked) { $ui.DoInstall.IsChecked = $false }
    }
    $any = $ui.DoAndroid.IsChecked -or $winTicked
    $ui.BuildBtn.IsEnabled = $script:GameOk -and $any
}
foreach ($c in @($ui.DoAndroid, $ui.DoWindows, $ui.DoPlain)) { $c.Add_Click({ Update-Setup }) }

$ui.ChangeGame.Add_Click({
    $d = New-Object System.Windows.Forms.FolderBrowserDialog
    $d.Description = "Choose the UFO 50 install directory (the folder with data.win in it)"
    if ($script:Game) { $d.SelectedPath = $script:Game }
    if ($d.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { Show-Game $d.SelectedPath }
})
$ui.GitHubBtn.Add_Click({ Start-Process "https://github.com/Patrick-Working/UFO50AndroidOnline" })

# ---- the jobs: one build.ps1 run per thing ticked ---------------------------------------------
$StepNames = [ordered]@{
    check = "Checking your game"; copy = "Copying the game"; download = "Downloading tools and mods"
    mods = "Adding the mods"; patch = "Patching the game for Android"; pack = "Packing the app"
    install = "Installing on your phone"; windows = "Putting the Windows version together"
    win7 = "Adding the Windows 7 fixes"
}
# build.ps1's "== ..." lines, and which step each one starts
$StepLines = @(
    @("^Finding UFO 50", "check"), @("^Copying the game", "copy"), @("^Downloading", "download"),
    @("^Applying the mods", "mods"), @("^Putting the Windows version together", "windows"),
    @("^Patching the game for Android", "patch"), @("^Packing and signing", "pack"), @("^Installing", "install"))

function New-Job($kind) {
    $a = @("-GamePath", $script:Game)
    $w7 = $true   # the Windows 7 fixups: always (they change nothing on 10 and 11)
    switch ($kind) {
        "android" { $t = "Android App"; $steps = @("check", "download", "mods", "patch", "pack"); if ($ui.DoInstall.IsChecked) { $a += "-Install"; $steps += "install" } }
        "windows" { $t = "Windows (UFO Online)"; $a += "-Windows"; $steps = @("check", "download", "mods", "windows"); if ($w7) { $steps += "win7" } else { $a += "-NoWin7" } }
        "plain"   { $t = "Windows (UFO Vanilla)"; $a += @("-Windows", "-Vanilla"); $steps = @("check", "copy"); if ($w7) { $steps += "win7" } else { $a += "-NoWin7" } }
    }
    $state = @{}; foreach ($s in $steps) { $state[$s] = "wait" }
    @{ Kind = $kind; Title = $t; Args = $a; Steps = $steps; State = $state; Rows = @{}; Out = ""; Id = ""; Error = ""; Warn = ""; Code = $null; Lines = (New-Object System.Collections.Generic.List[string]) }
}

function Quote($s) { if ($s -match '[\s"]') { '"' + ($s -replace '"', '\"') + '"' } else { $s } }

$script:Jobs = @(); $script:Current = -1; $script:Run = $null; $script:Started = $null; $script:Stopped = $false

function Glyph($st) {
    switch ($st) { "done" { @([char]0x2713, $Green) } "skip" { @([char]0x2713, $Muted) } "run" { @([char]0x25CF, $Blue) } "fail" { @([char]0x2715, $Orange) } default { @([char]0x25CB, "#6F6F6F") } }
}
function Draw-Steps {
    $ui.Steps.Children.Clear()
    $total = 0; $done = 0
    foreach ($j in $script:Jobs) {
        $h = New-Object Windows.Controls.TextBlock
        $h.Text = $j.Title.ToUpper(); $h.FontSize = 12; $h.Foreground = Brush $Muted; $h.Margin = "0,14,0,6"
        $ui.Steps.Children.Add($h) | Out-Null
        foreach ($s in $j.Steps) {
            $st = $j.State[$s]; $g = Glyph $st
            $row = New-Object Windows.Controls.DockPanel; $row.Margin = "0,0,0,5"
            $i = New-Object Windows.Controls.TextBlock; $i.Text = $g[0]; $i.Foreground = Brush $g[1]; $i.Width = 22; $i.FontFamily = "Segoe UI Symbol"
            $l = New-Object Windows.Controls.TextBlock; $l.Text = $StepNames[$s]; $l.Foreground = Brush "#FFFFFF"
            if ($st -eq "run") { $l.Foreground = Brush $Blue; $l.Text += "..." }
            elseif ($st -eq "wait") { $l.Foreground = Brush $Muted }
            elseif ($st -eq "skip") { $l.Foreground = Brush $Muted; $l.Text += "  (already done)" }
            elseif ($st -eq "fail") { $l.Foreground = Brush "#FEB854" }
            $row.Children.Add($i) | Out-Null; $row.Children.Add($l) | Out-Null
            $ui.Steps.Children.Add($row) | Out-Null
            $total++; if ($st -eq "done" -or $st -eq "skip") { $done++ } elseif ($st -eq "run") { $done += 0.5 }
        }
    }
    $w = $ui.Shot.ActualWidth - 56; if ($w -lt 100) { $w = 564 }
    $ui.Bar.Width = if ($total) { [math]::Max(0, $w * $done / $total) } else { 0 }
}

function Start-Step($j, $key) {
    if (-not $j.State.ContainsKey($key)) { return }
    foreach ($s in $j.Steps) {
        if ($s -eq $key) { break }
        if ($j.State[$s] -eq "run") { $j.State[$s] = "done" } elseif ($j.State[$s] -eq "wait") { $j.State[$s] = "skip" }
    }
    $j.State[$key] = "run"
}

function Read-Line($j, $line) {
    $j.Lines.Add($line)
    $t = $line -replace "\x1b\[[0-9;]*m", ""
    if ($t -match "^== (.*)") {
        foreach ($m in $StepLines) { if ($Matches[1] -match $m[0]) { Start-Step $j $m[1]; break } }
    }
    elseif ($t -match "menu font:|without Steam's libraries") { Start-Step $j "win7" }
    elseif ($t -match "^Done: (.+?)(?: is ready for Windows 7.*)?$") { $j.Out = $Matches[1] }
    elseif ($t -match "build identity: (\S+)") { $j.Id = $Matches[1] }
    elseif ($t -match "^ERROR: (.*)") { $j.Error = $Matches[1] }
    elseif ($t -match "Windows 7 (fixups: didn't go in|check failed)") { $j.Warn = "The Windows 7 fixes didn't go in: this folder starts on Windows 10 and 11 only." }
    elseif ($t -match "The UFO 50 Mod Loader is opening") {
        Show-Banner "The Mod Loader Opened in Its Own Window" "Make sure PFO 50 and MultiMadnessMod are ticked, click Install, wait until it says it's done, then close it. The build carries on after that." $Banner
    }
    $ui.Log.AppendText($t + "`r`n")
}

function Show-Banner($title, $text, $color) {
    $ui.Banner.Visibility = "Visible"; $ui.Banner.Background = Brush $color
    $ui.BannerTitle.Text = $title; $ui.BannerText.Text = $text
    $ui.BannerText.Visibility = if ($text) { "Visible" } else { "Collapsed" }
}

# build.ps1's ERROR lines, said for someone who didn't write it
function Explain($j) {
    $e = $j.Error
    if ($script:Stopped) { return "You stopped the build. What was done so far is kept, so the next build is quicker." }
    if (-not $e) { return "build.ps1 stopped without saying why (exit code $($j.Code)). Show details has everything it printed." }
    if ($e -match "UFO 50 not found") { return "UFO 50 wasn't found. Choose its install directory on the first screen." }
    if ($e -match "different version of UFO 50") { return "Your UFO 50 isn't version $GameVersion. Update it in Steam and try again." }
    if ($e -match "^download failed") { return "A download didn't work. Check your internet connection and try again." }
    if ($e -match "doesn't match its checksum") { return "A download isn't the file this version expects: it may have changed on the website. Try again later, or ask on GitHub." }
    if ($e -match "is running") { return "That game is running. Close it, then build again: the build can't replace a game that's open." }
    if ($e -match "^adb") { return "The app was made but couldn't go on your phone. Is it connected with USB debugging on? If it already has this app from someone else's build, uninstall that first. The app file is in the work folder: you can also copy it over yourself." }
    if ($e -match "mods aren't in") { return "The mods didn't go in. Try again; if the Mod Loader's window opens, follow its steps." }
    if ($e -match "isn't whole") { return "The copy of the game isn't complete. Is the disk full?" }
    if ($e -match "\[ or \]") { return "A folder name has [ or ] in it, which the build can't work with. Move the folder and try again." }
    return ($e.Substring(0, 1).ToUpper() + $e.Substring(1))
}

function Start-Next {
    $script:Current++
    if ($script:Current -ge $script:Jobs.Count) { Finish; return }
    $j = $script:Jobs[$script:Current]
    $ui.Log.AppendText("---- $($j.Title) ----`r`n")
    $cmd = "-NoProfile -ExecutionPolicy Bypass -File " + (Quote $BuildScript) + " " + (($j.Args | ForEach-Object { Quote $_ }) -join " ")
    $script:Run = New-Object BuildRun
    $script:Run.Start("powershell.exe", $cmd, $Root)
    Draw-Steps
}

$timer = New-Object Windows.Threading.DispatcherTimer
$timer.Interval = [TimeSpan]::FromMilliseconds(250)
$timer.Add_Tick({
    if (-not $script:Run) { return }
    $j = $script:Jobs[$script:Current]
    $lines = $script:Run.Take()
    foreach ($l in $lines) { Read-Line $j $l }
    $el = (Get-Date) - $script:Started
    $ui.Subtitle.Text = "Building... {0}:{1:00}" -f [int][math]::Floor($el.TotalMinutes), $el.Seconds
    if ($script:Run.Finished()) {
        foreach ($l in $script:Run.Take()) { Read-Line $j $l }
        $j.Code = $script:Run.Code(); $script:Run = $null
        if ($j.Code -eq 0 -and -not $script:Stopped) {
            foreach ($s in $j.Steps) { if ($j.State[$s] -eq "run") { $j.State[$s] = "done" } elseif ($j.State[$s] -eq "wait") { $j.State[$s] = "skip" } }
            Start-Next
        } else {
            foreach ($s in $j.Steps) { if ($j.State[$s] -eq "run") { $j.State[$s] = "fail" } }
            Finish
        }
    }
    if ($lines.Count -and $script:Run) {
        Draw-Steps
        $past = @($j.State.Values | Where-Object { $_ -eq "done" -or $_ -eq "skip" }).Count
        if ($past -ge 2 -and ($j.State.Values -contains "run")) { Take-Shot "2-building" }
    }
    if ($lines.Count) { $ui.Log.ScrollToEnd() }
})

function Size-Text($path) {
    if (Test-Path -LiteralPath $path -PathType Leaf) { return "{0:N0} MB" -f ((Get-Item -LiteralPath $path).Length / 1MB) }
    return ""
}

function Add-Result($j) {
    $card = New-Object Windows.Controls.Border
    $card.CornerRadius = 6; $card.Padding = "12,10"; $card.Margin = "0,12,0,0"; $card.Background = Brush "#202040"
    $sp = New-Object Windows.Controls.StackPanel
    $head = New-Object Windows.Controls.DockPanel
    $btns = New-Object Windows.Controls.StackPanel; $btns.Orientation = "Horizontal"; [Windows.Controls.DockPanel]::SetDock($btns, "Right")
    $t = New-Object Windows.Controls.TextBlock; $t.FontWeight = "SemiBold"; $t.VerticalAlignment = "Center"
    $out = $j.Out
    switch ($j.Kind) {
        "android" {
            $t.Text = "$([char]0x2713)  Android App  " + (Size-Text $out)
            $note = "Copy it to your phone and open it there to install (allow installing apps from your file manager if it asks)."
            if ($j.Args -contains "-Install") { $note = "It's installed on your phone: open UFO 50 Online there." }
            $b = New-Object Windows.Controls.Button; $b.Content = "Show the file"; $b.Tag = $out
            $b.Add_Click({ param($s) Start-Process explorer.exe "/select,`"$($s.Tag)`"" }); $btns.Children.Add($b) | Out-Null
            if ($script:Adb -and $script:Phones -gt 0 -and -not ($j.Args -contains "-Install")) {
                $b2 = New-Object Windows.Controls.Button; $b2.Content = "Install on phone"; $b2.Margin = "8,0,0,0"; $b2.Tag = $out
                $b2.Add_Click({ param($s) Install-Apk $s }); $btns.Children.Add($b2) | Out-Null
            }
        }
        "windows" {
            $t.Text = "$([char]0x2713)  Windows (UFO Online)"
            $note = "Start ufo50.exe in the folder (Windows 7, 10 or 11); F1 opens the online menu. Its saves are kept apart from the Steam game's."
            $b = New-Object Windows.Controls.Button; $b.Content = "Open the folder"; $b.Tag = $out
            $b.Add_Click({ param($s) Start-Process explorer.exe "`"$($s.Tag)`"" }); $btns.Children.Add($b) | Out-Null
        }
        "plain" {
            $t.Text = "$([char]0x2713)  Windows (UFO Vanilla)"
            $note = "Start ""UFO 50 Portable.bat"" in the folder: the saves then stay in the folder, so it can go on a USB stick or another PC."
            $b = New-Object Windows.Controls.Button; $b.Content = "Open the folder"; $b.Tag = $out
            $b.Add_Click({ param($s) Start-Process explorer.exe "`"$($s.Tag)`"" }); $btns.Children.Add($b) | Out-Null
        }
    }
    $head.Children.Add($btns) | Out-Null; $head.Children.Add($t) | Out-Null
    $sp.Children.Add($head) | Out-Null
    $n = New-Object Windows.Controls.TextBlock; $n.Style = $win.FindResource("Hint"); $n.Margin = "0,6,0,0"; $n.Text = $note
    $sp.Children.Add($n) | Out-Null
    if ($j.Warn) { $w = New-Object Windows.Controls.TextBlock; $w.Foreground = Brush "#FEB854"; $w.FontSize = 12; $w.Margin = "0,4,0,0"; $w.Text = $j.Warn; $sp.Children.Add($w) | Out-Null }
    $p = New-Object Windows.Controls.TextBlock; $p.Style = $win.FindResource("Hint"); $p.Margin = "0,4,0,0"; $p.Text = $out; $p.FontFamily = "Consolas"; $p.FontSize = 11
    $sp.Children.Add($p) | Out-Null
    $card.Child = $sp
    $ui.Results.Children.Add($card) | Out-Null
}

function Install-Apk($btn) {
    $btn.IsEnabled = $false; $btn.Content = "Installing..."
    $win.Dispatcher.Invoke([action]{}, "Render")
    $r = & $script:Adb -s $script:Serial install -r $btn.Tag 2>&1
    $btn.Content = if ($LASTEXITCODE -eq 0) { "Installed" } else { "Didn't install" }
    if ($LASTEXITCODE -ne 0) { Show-Banner "It Couldn't Go on Your Phone" "Is it connected with USB debugging on? If it already has this app from someone else's build, uninstall that first. ($(($r | Select-Object -Last 1)))" $Banner }
}

function Finish {
    $timer.Stop()
    $ui.StopBtn.Visibility = "Collapsed"; $ui.CloseBtn.Visibility = "Visible"; $ui.AgainBtn.Visibility = "Visible"; $ui.CopyBtn.Visibility = "Visible"
    $failed = @($script:Jobs | Where-Object { $_.Code -ne $null -and $_.Code -ne 0 }) + @($script:Jobs | Where-Object { $script:Stopped -and $_.Code -eq $null })
    $done = @($script:Jobs | Where-Object { $_.Code -eq 0 })
    $el = (Get-Date) - $script:Started
    $took = "{0}:{1:00}" -f [int][math]::Floor($el.TotalMinutes), $el.Seconds
    if ($failed.Count -eq 0) {
        $ui.Subtitle.Text = "Done in $took."
        $id = ($script:Jobs | Where-Object { $_.Id } | Select-Object -First 1).Id
        $ui.Banner.Visibility = "Collapsed"
        if ($id) { $ui.Subtitle.Text += "  Build $id" }
        $ui.Steps.Visibility = "Collapsed"; $ui.Bar.Parent.Visibility = "Collapsed"
        foreach ($j in $done) { Add-Result $j }
        $k = New-Object Windows.Controls.TextBlock; $k.Style = $win.FindResource("Hint"); $k.Margin = "0,14,0,0"
        $k.Text = "These contain your copy of UFO 50: keep them to yourself. Friends build their own from their copy: everyone needs the same version to play together online (this build: $Version)."
        $ui.Results.Children.Add($k) | Out-Null
        Take-Shot "3-done"
    } else {
        $j = $failed[0]
        $ui.Subtitle.Text = "Stopped after $took."
        $title = if ($script:Stopped) { "Build Stopped" } else { "$($j.Title): The Build Stopped" }
        Show-Banner $title (Explain $j) $Banner
        foreach ($d in $done) { Add-Result $d }
        Draw-Steps
        Take-Shot "4-stopped"
    }
    if ($Test) { $t2 = New-Object Windows.Threading.DispatcherTimer; $t2.Interval = [TimeSpan]::FromSeconds(1); $t2.Add_Tick({ $this.Stop(); $win.Close() }); $t2.Start() }
}

$ui.BuildBtn.Add_Click({
    $script:Jobs = @()
    if ($ui.DoAndroid.IsChecked) { $script:Jobs += New-Job "android" }
    if ($ui.DoWindows.IsChecked) { $script:Jobs += New-Job "windows" }
    if ($ui.DoPlain.IsChecked) { $script:Jobs += New-Job "plain" }
    if (-not $script:Jobs.Count) { return }
    $ui.PageSetup.Visibility = "Collapsed"; $ui.PageRun.Visibility = "Visible"; $ui.CreditsBox.Visibility = "Collapsed"
    $ui.Results.Children.Clear(); $ui.Log.Clear(); $ui.Banner.Visibility = "Collapsed"; $ui.Steps.Visibility = "Visible"; $ui.Bar.Parent.Visibility = "Visible"
    $ui.StopBtn.Visibility = "Visible"; $ui.CloseBtn.Visibility = "Collapsed"; $ui.AgainBtn.Visibility = "Collapsed"; $ui.CopyBtn.Visibility = "Collapsed"
    $script:Current = -1; $script:Stopped = $false; $script:Started = Get-Date
    Start-Next
    $timer.Start()
})
$ui.StopBtn.Add_Click({
    if (-not $script:Run) { return }
    $a = [System.Windows.MessageBox]::Show("Stop the build? What's done so far is kept, so the next one is quicker.", "UFO 50 Android Online Builder", "YesNo", "Question")
    if ($a -eq "Yes") { $script:Stopped = $true; $script:Run.Stop() }
})
$ui.LogBtn.Add_Click({
    if ($ui.Log.Visibility -eq "Visible") { $ui.Log.Visibility = "Collapsed"; $ui.LogBtn.Content = "Show details" }
    else { $ui.Log.Visibility = "Visible"; $ui.LogBtn.Content = "Hide details"; $ui.Log.ScrollToEnd() }
})
$ui.CopyBtn.Add_Click({ [System.Windows.Clipboard]::SetText($ui.Log.Text); $ui.CopyBtn.Content = "Copied" })
$ui.CloseBtn.Add_Click({ $win.Close() })
$ui.AgainBtn.Add_Click({
    $timer.Stop(); $ui.PageRun.Visibility = "Collapsed"; $ui.PageSetup.Visibility = "Visible"; $ui.CreditsBox.Visibility = "Visible"
    $ui.Subtitle.Text = "Script to build UFO 50 Android Online."; $ui.CopyBtn.Content = "Copy details"
})
$win.Add_Closing({
    param($s, $e)
    if ($script:Run -and -not $Test) {
        $a = [System.Windows.MessageBox]::Show("A build is running. Stop it and close?", "UFO 50 Android Online Builder", "YesNo", "Question")
        if ($a -ne "Yes") { $e.Cancel = $true; return }
    }
    if ($script:Run) { $script:Run.Stop() }
})

# ---- pictures of the window, for the docs and for review (-Shots) -------------------------------
$script:ShotsTaken = @{}
function Take-Shot($name) {
    if (-not $Shots -or $script:ShotsTaken[$name]) { return }
    $script:ShotsTaken[$name] = $true
    New-Item -ItemType Directory -Force $Shots | Out-Null
    $win.UpdateLayout()
    $e = $ui.Shot; $scale = 1.5
    $bmp = New-Object Windows.Media.Imaging.RenderTargetBitmap ([int]($e.ActualWidth * $scale)), ([int]($e.ActualHeight * $scale)), (96 * $scale), (96 * $scale), ([Windows.Media.PixelFormats]::Pbgra32)
    $bmp.Render($e)
    $enc = New-Object Windows.Media.Imaging.PngBitmapEncoder
    $enc.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bmp))
    $f = [IO.File]::Create((Join-Path $Shots "$name.png")); $enc.Save($f); $f.Close()
    $sv = $ui.Shot.Parent
    Add-Content -LiteralPath (Join-Path $Shots "sizes.txt") ("{0}: window {1}x{2} (max {3}), scrollbar {4}" -f $name, [int]$win.ActualWidth, [int]$win.ActualHeight, [int]$win.MaxHeight, $sv.ComputedVerticalScrollBarVisibility)
}

$win.Add_ContentRendered({
    $ui.GameTitle.Text = "Looking for UFO 50..."
    $win.Dispatcher.Invoke([action]{}, "Render")
    Check-Phone
    if ($GamePath) { Show-Game $GamePath } else { Show-Game (Find-Game) }
    if ($Test) {
        $want = $Test -split ","
        $ui.DoAndroid.IsChecked = $want -contains "android"; $ui.DoWindows.IsChecked = $want -contains "windows"; $ui.DoPlain.IsChecked = $want -contains "plain"
        Update-Setup
        Take-Shot "1-setup"
        if ($ui.BuildBtn.IsEnabled) { $ui.BuildBtn.RaiseEvent((New-Object Windows.RoutedEventArgs ([Windows.Controls.Button]::ClickEvent))) }
        else { $win.Close() }
    }
})
Update-Setup
$win.MaxHeight = [System.Windows.SystemParameters]::WorkArea.Height
$win.ShowDialog() | Out-Null
