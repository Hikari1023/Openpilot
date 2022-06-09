#include "selfdrive/ui/qt/offroad/settings.h"

#include <cassert>
#include <cmath>
#include <string>

#include <QDebug>

#ifndef QCOM
#include "selfdrive/ui/qt/offroad/networking.h"
#endif

#ifdef ENABLE_MAPS
#include "selfdrive/ui/qt/maps/map_settings.h"
#endif

#include "selfdrive/common/params.h"
#include "selfdrive/common/util.h"
#include "selfdrive/hardware/hw.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/input.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/widgets/ssh_keys.h"
#include "selfdrive/ui/qt/widgets/timpilot.h"
#include "selfdrive/ui/qt/widgets/toggle.h"
#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/qt_window.h"

TogglesPanel::TogglesPanel(SettingsWindow *parent) : ListWidget(parent) {
  // param, title, desc, icon
  std::vector<std::tuple<QString, QString, QString, QString>> toggles{
    {
      "OpenpilotEnabledToggle",
      "開啟 Openpilot",
      "使用Openpilot的主動式巡航和車道維持功能，開啟後您仍需保持注意力集中，設定變更後需重啟車輛以生效",
      "../assets/offroad/icon_openpilot.png",
    },
    {
      "IsLdwEnabled",
      "啟用車道偏移警告",
      "車速在 50 km/h (31 mph)以上且未打轉向燈的情況下，如果偵測到車輛駛出目前的車道線時，將發出車道偏離警示",
      "../assets/offroad/icon_warning.png",
    },
    {
      "IsRHD",
      "啟用右駕模式",
      "允許 Openpilot 遵守靠左駕的交通慣例同時對右側駕駛座上的駕駛執行監控",
      "../assets/offroad/icon_openpilot_mirrored.png",
    },
    {
      "IsMetric",
      "使用公制單位",
      "開啟後將以公制單位 km/h 取代 mph",
      "../assets/offroad/icon_metric.png",
    },
    {
      "RecordFront",
      "記錄並上傳駕駛鏡頭影像",
      "上傳前置相機的駕駛錄像來幫助我們提升駕駛監控的準確度",
      "../assets/offroad/icon_monitoring.png",
    },
    {
      "EndToEndToggle",
      "\U0001f96c 啟用無線道模式 e2e (Alpha) \U0001f96c",
      "在這個模式下 Openpilot 將不依賴車道線進行駕駛，而是以模擬人為的方式進行駕駛",
      "../assets/offroad/icon_road.png",
    },
    {
      "DisengageOnAccelerator",
      "不允許踩油門",
      "開啟這個選項，踩下油門踏板將會解除 Openpilot",
      "../assets/offroad/icon_disengage_on_accelerator.svg",
    },
#ifdef ENABLE_MAPS
    {
      "NavSettingTime24h",
      "預計到達時間改用24小時制",
      "使用 24 小時制取代 am/pm",
      "../assets/offroad/icon_metric.png",
    },
#endif

  };

  Params params;

  if (params.getBool("DisableRadar_Allow")) {
    toggles.push_back({
      "DisableRadar",
      "Openpilot 蹤向控制",
      "Openpilot 將禁用汽車的雷達並接管油門和剎車的控制。警告：這會禁用 AEB！",
      "../assets/offroad/icon_speed_limit.png",
    });
  }

  for (auto &[param, title, desc, icon] : toggles) {
    auto toggle = new ParamControl(param, title, desc, icon, this);
    bool locked = params.getBool((param + "Lock").toStdString());
    toggle->setEnabled(!locked);
    if (!locked) {
      connect(uiState(), &UIState::offroadTransition, toggle, &ParamControl::setEnabled);
    }
    addItem(toggle);
  }
}

DevicePanel::DevicePanel(SettingsWindow *parent) : ListWidget(parent) {
  setSpacing(50);
  addItem(new LabelControl("裝置 ID", getDongleId().value_or("N/A")));
  addItem(new LabelControl("序號", params.get("HardwareSerial").c_str()));

  // offroad-only buttons

  auto dcamBtn = new ButtonControl("駕駛監控", "預覽",
                                   "預覽前置相機的駕駛影像，以最佳化設備安裝位置獲得最佳駕駛監控體驗。 （車輛必須關閉）");
  connect(dcamBtn, &ButtonControl::clicked, [=]() { emit showDriverView(); });
  addItem(dcamBtn);

  auto resetCalibBtn = new ButtonControl("重置校準", "重置", " ");
  connect(resetCalibBtn, &ButtonControl::showDescription, this, &DevicePanel::updateCalibDescription);
  connect(resetCalibBtn, &ButtonControl::clicked, [&]() {
    if (ConfirmationDialog::confirm("您確定要重置校準嗎？", this)) {
      params.remove("CalibrationParams");
    }
  });
  addItem(resetCalibBtn);

  if (!params.getBool("Passive")) {
    auto retrainingBtn = new ButtonControl("回顧使用教學", "查看", "Review the rules, features, and limitations of openpilot");
    connect(retrainingBtn, &ButtonControl::clicked, [=]() {
      if (ConfirmationDialog::confirm("您確定要瀏覽訓練指南嗎？", this)) {
        emit reviewTrainingGuide();
      }
    });
    addItem(retrainingBtn);
  }

  if (Hardware::TICI()) {
    auto regulatoryBtn = new ButtonControl("認證資訊", "查看", "");
    connect(regulatoryBtn, &ButtonControl::clicked, [=]() {
      const std::string txt = util::read_file("../assets/offroad/fcc.html");
      RichTextDialog::alert(QString::fromStdString(txt), this);
    });
    addItem(regulatoryBtn);
  }

  QObject::connect(uiState(), &UIState::offroadTransition, [=](bool offroad) {
    for (auto btn : findChildren<ButtonControl *>()) {
      btn->setEnabled(offroad);
    }
  });

  // power buttons
  QHBoxLayout *power_layout = new QHBoxLayout();
  power_layout->setSpacing(30);

  QPushButton *reboot_btn = new QPushButton("重新開機");
  reboot_btn->setObjectName("reboot_btn");
  power_layout->addWidget(reboot_btn);
  QObject::connect(reboot_btn, &QPushButton::clicked, this, &DevicePanel::reboot);

  QPushButton *poweroff_btn = new QPushButton("關機");
  poweroff_btn->setObjectName("poweroff_btn");
  power_layout->addWidget(poweroff_btn);
  QObject::connect(poweroff_btn, &QPushButton::clicked, this, &DevicePanel::poweroff);

  if (Hardware::TICI()) {
    connect(uiState(), &UIState::offroadTransition, poweroff_btn, &QPushButton::setVisible);
  }

  setStyleSheet(R"(
    #reboot_btn { height: 120px; border-radius: 15px; background-color: #393939; }
    #reboot_btn:pressed { background-color: #4a4a4a; }
    #poweroff_btn { height: 120px; border-radius: 15px; background-color: #E22C2C; }
    #poweroff_btn:pressed { background-color: #FF2424; }
  )");
  addItem(power_layout);
}

void DevicePanel::updateCalibDescription() {
  QString desc =
      "Openpilot 要求設備安裝在左、右 4° 誤差範圍內，並且 "
      "上 5° 或下 8° 誤差範圍以內。 Openpilot 會持續校準，很少需要重置。";
  std::string calib_bytes = Params().get("CalibrationParams");
  if (!calib_bytes.empty()) {
    try {
      AlignedBuffer aligned_buf;
      capnp::FlatArrayMessageReader cmsg(aligned_buf.align(calib_bytes.data(), calib_bytes.size()));
      auto calib = cmsg.getRoot<cereal::Event>().getLiveCalibration();
      if (calib.getCalStatus() != 0) {
        double pitch = calib.getRpyCalib()[1] * (180 / M_PI);
        double yaw = calib.getRpyCalib()[2] * (180 / M_PI);
        desc += QString(" 您的設備偏差 %1° %2 and %3° %4.")
                    .arg(QString::number(std::abs(pitch), 'g', 1), pitch > 0 ? "下" : "上",
                         QString::number(std::abs(yaw), 'g', 1), yaw > 0 ? "左" : "右");
      }
    } catch (kj::Exception) {
      qInfo() << "invalid CalibrationParams";
    }
  }
  qobject_cast<ButtonControl *>(sender())->setDescription(desc);
}

void DevicePanel::reboot() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm("您確定要重新開機?", this)) {
      // Check engaged again in case it changed while the dialog was open
      if (!uiState()->engaged()) {
        Params().putBool("DoReboot", true);
      }
    }
  } else {
    ConfirmationDialog::alert("停止重新開機", this);
  }
}

void DevicePanel::poweroff() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm("你確定要關機嗎?", this)) {
      // Check engaged again in case it changed while the dialog was open
      if (!uiState()->engaged()) {
        Params().putBool("DoShutdown", true);
      }
    }
  } else {
    ConfirmationDialog::alert("停止關機", this);
  }
}

SoftwarePanel::SoftwarePanel(QWidget* parent) : ListWidget(parent) {
  gitBranchLbl = new LabelControl("Git 分支");
  gitCommitLbl = new LabelControl("Git 提交描述");
  osVersionLbl = new LabelControl("系統版本");
  versionLbl = new LabelControl("版本", "", QString::fromStdString(params.get("ReleaseNotes")).trimmed());
  lastUpdateLbl = new LabelControl("上次檢查更新時間", "", "上次 Openpilot 成功檢查更新的時間。更新程序僅在車輛關閉時運行.");
  updateBtn = new ButtonControl("檢查更新", "");
  connect(updateBtn, &ButtonControl::clicked, [=]() {
    if (params.getBool("IsOffroad")) {
      fs_watch->addPath(QString::fromStdString(params.getParamPath("LastUpdateTime")));
      fs_watch->addPath(QString::fromStdString(params.getParamPath("UpdateFailedCount")));
      updateBtn->setText("檢查");
      updateBtn->setEnabled(false);
    }
    std::system("pkill -1 -f selfdrive.updated");
  });


  auto uninstallBtn = new ButtonControl("移除" + getBrand(), "移除");
  connect(uninstallBtn, &ButtonControl::clicked, [&]() {
    if (ConfirmationDialog::confirm("您確定要移除嗎？", this)) {
      params.putBool("DoUninstall", true);
    }
  });
  connect(uiState(), &UIState::offroadTransition, uninstallBtn, &QPushButton::setEnabled);

  QWidget *widgets[] = {versionLbl, lastUpdateLbl, updateBtn, gitBranchLbl, gitCommitLbl, osVersionLbl, uninstallBtn};
  for (QWidget* w : widgets) {
    addItem(w);
  }

  fs_watch = new QFileSystemWatcher(this);
  QObject::connect(fs_watch, &QFileSystemWatcher::fileChanged, [=](const QString path) {
    if (path.contains("UpdateFailedCount") && std::atoi(params.get("UpdateFailedCount").c_str()) > 0) {
      lastUpdateLbl->setText("failed to fetch update");
      updateBtn->setText("CHECK");
      updateBtn->setEnabled(true);
    } else if (path.contains("LastUpdateTime")) {
      updateLabels();
    }
  });
}

void SoftwarePanel::showEvent(QShowEvent *event) {
  updateLabels();
}

void SoftwarePanel::updateLabels() {
  QString lastUpdate = "";
  auto tm = params.get("LastUpdateTime");
  if (!tm.empty()) {
    lastUpdate = timeAgo(QDateTime::fromString(QString::fromStdString(tm + "Z"), Qt::ISODate));
  }

  versionLbl->setText(getBrandVersion());
  lastUpdateLbl->setText(lastUpdate);
  updateBtn->setText("CHECK");
  updateBtn->setEnabled(true);
  gitBranchLbl->setText(QString::fromStdString(params.get("GitBranch")));
  gitCommitLbl->setText(QString::fromStdString(params.get("GitCommit")).left(10));
  osVersionLbl->setText(QString::fromStdString(Hardware::get_os_version()).trimmed());
}

C2NetworkPanel::C2NetworkPanel(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(50, 0, 50, 0);

  ListWidget *list = new ListWidget();
  list->setSpacing(30);
  // wifi + tethering buttons
#ifdef QCOM
  auto wifiBtn = new ButtonControl("Wi-Fi 設定", "開啟");
  QObject::connect(wifiBtn, &ButtonControl::clicked, [=]() { HardwareEon::launch_wifi(); });
  list->addItem(wifiBtn);

  auto tetheringBtn = new ButtonControl("網絡共享設置", "開啟");
  QObject::connect(tetheringBtn, &ButtonControl::clicked, [=]() { HardwareEon::launch_tethering(); });
  list->addItem(tetheringBtn);
#endif
  ipaddress = new LabelControl("IP 位置", "");
  list->addItem(ipaddress);

  // SSH key management
  list->addItem(new SshToggle());
  list->addItem(new SshControl());
  layout->addWidget(list);
  layout->addStretch(1);
}

void C2NetworkPanel::showEvent(QShowEvent *event) {
  ipaddress->setText(getIPAddress());
}

QString C2NetworkPanel::getIPAddress() {
  std::string result = util::check_output("ifconfig wlan0");
  if (result.empty()) return "";

  const std::string inetaddrr = "inet addr:";
  std::string::size_type begin = result.find(inetaddrr);
  if (begin == std::string::npos) return "";

  begin += inetaddrr.length();
  std::string::size_type end = result.find(' ', begin);
  if (end == std::string::npos) return "";

  return result.substr(begin, end - begin).c_str();
}

QWidget *network_panel(QWidget *parent) {
#ifdef QCOM
  return new C2NetworkPanel(parent);
#else
  return new Networking(parent);
#endif
}

void SettingsWindow::showEvent(QShowEvent *event) {
  panel_widget->setCurrentIndex(0);
  nav_btns->buttons()[0]->setChecked(true);
}

SettingsWindow::SettingsWindow(QWidget *parent) : QFrame(parent) {

  // setup two main layouts
  sidebar_widget = new QWidget;
  QVBoxLayout *sidebar_layout = new QVBoxLayout(sidebar_widget);
  sidebar_layout->setMargin(0);
  panel_widget = new QStackedWidget();
  panel_widget->setStyleSheet(R"(
    border-radius: 30px;
    background-color: #292929;
  )");

  // close button
  QPushButton *close_btn = new QPushButton("×");
  close_btn->setStyleSheet(R"(
    QPushButton {
      font-size: 120px;
      padding-bottom: 20px;
      font-weight: bold;
      border 1px grey solid;
      border-radius: 100px;
      background-color: #292929;
      font-weight: 400;
    }
    QPushButton:pressed {
      background-color: #3B3B3B;
    }
  )");
  close_btn->setFixedSize(140, 140);
  sidebar_layout->addSpacing(40);
  sidebar_layout->addWidget(close_btn, 0, Qt::AlignLeft);
  QObject::connect(close_btn, &QPushButton::clicked, this, &SettingsWindow::closeSettings);

  // setup panels
  DevicePanel *device = new DevicePanel(this);
  QObject::connect(device, &DevicePanel::reviewTrainingGuide, this, &SettingsWindow::reviewTrainingGuide);
  QObject::connect(device, &DevicePanel::showDriverView, this, &SettingsWindow::showDriverView);

  QList<QPair<QString, QWidget *>> panels = {
    {"設備", device},
    {"網路", network_panel(this)},
    {"設定", new TogglesPanel(this)},
    {"軟體", new SoftwarePanel(this)},
    {"T.O.P 功能", new TimpilotPanel(this)},
  };

#ifdef ENABLE_MAPS
  auto map_panel = new MapPanel(this);
  panels.push_back({"導航地圖", map_panel});
  QObject::connect(map_panel, &MapPanel::closeSettings, this, &SettingsWindow::closeSettings);
#endif

  const int padding = panels.size() > 3 ? 15 : 35;

  nav_btns = new QButtonGroup(this);
  for (auto &[name, panel] : panels) {
    QPushButton *btn = new QPushButton(name);
    btn->setCheckable(true);
    btn->setChecked(nav_btns->buttons().size() == 0);
    btn->setStyleSheet(QString(R"(
      QPushButton {
        color: grey;
        border: none;
        background: none;
        font-size: 65px;
        font-weight: 500;
        padding-top: %1px;
        padding-bottom: %1px;
      }
      QPushButton:checked {
        color: white;
      }
      QPushButton:pressed {
        color: #ADADAD;
      }
    )").arg(padding));

    nav_btns->addButton(btn);
    sidebar_layout->addWidget(btn, 0, Qt::AlignRight);

    const int lr_margin = name != "Network" ? 50 : 0;  // Network panel handles its own margins
    panel->setContentsMargins(lr_margin, 25, lr_margin, 25);

    ScrollView *panel_frame = new ScrollView(panel, this);
    panel_widget->addWidget(panel_frame);

    QObject::connect(btn, &QPushButton::clicked, [=, w = panel_frame]() {
      btn->setChecked(true);
      panel_widget->setCurrentWidget(w);
    });
  }
  sidebar_layout->setContentsMargins(50, 50, 100, 50);

  // main settings layout, sidebar + main panel
  QHBoxLayout *main_layout = new QHBoxLayout(this);

  sidebar_widget->setFixedWidth(500);
  main_layout->addWidget(sidebar_widget);
  main_layout->addWidget(panel_widget);

  setStyleSheet(R"(
    * {
      color: white;
      font-size: 50px;
    }
    SettingsWindow {
      background-color: black;
    }
  )");
}

void SettingsWindow::hideEvent(QHideEvent *event) {
#ifdef QCOM
  HardwareEon::close_activities();
#endif
}

TimpilotPanel::TimpilotPanel(QWidget* parent) : QWidget(parent) {
  main_layout = new QStackedLayout(this);
  home = new QWidget(this);
  QVBoxLayout* fcr_layout = new QVBoxLayout(home);
  fcr_layout->setContentsMargins(0, 20, 0, 20);

  QString set = QString::fromStdString(Params().get("CarModel"));

  QPushButton* setCarBtn = new QPushButton(set.length() ? set : "選擇車款");
  setCarBtn->setObjectName("setCarBtn");
  setCarBtn->setStyleSheet("margin-right: 30px;");
  connect(setCarBtn, &QPushButton::clicked, [=]() { main_layout->setCurrentWidget(setCar); });
  fcr_layout->addSpacing(10);
  fcr_layout->addWidget(setCarBtn, 0, Qt::AlignRight);
  fcr_layout->addSpacing(10);

  home_widget = new QWidget(this);
  QVBoxLayout* toggle_layout = new QVBoxLayout(home_widget);
  home_widget->setObjectName("homeWidget");

  ScrollView *scroller = new ScrollView(home_widget, this);
  scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  fcr_layout->addWidget(scroller, 1);

  main_layout->addWidget(home);

  setCar = new ForceCarRecognition(this);
  connect(setCar, &ForceCarRecognition::backPress, [=]() { main_layout->setCurrentWidget(home); });
  connect(setCar, &ForceCarRecognition::selectedCar, [=]() {
    QString set = QString::fromStdString(Params().get("CarModel"));
    setCarBtn->setText(set.length() ? set : "選擇您的車款");
    main_layout->setCurrentWidget(home);
  });
  main_layout->addWidget(setCar);

  QPalette pal = palette();
  pal.setColor(QPalette::Background, QColor(0x29, 0x29, 0x29));
  setAutoFillBackground(true);
  setPalette(pal);

  setStyleSheet(R"(
    #backBtn, #setCarBtn {
      font-size: 50px;
      margin: 0px;
      padding: 20px;
      border-width: 0;
      border-radius: 30px;
      color: #dddddd;
      background-color: #444444;
    }
  )");

  QList<ParamControl*> toggles;

  toggles.append(new ParamControl("QuietDrive",
                                  "安靜模式",
                                  "Openpilot 將只會針對最重要的警示發出警告聲音。可以在汽車啟動時切換此功能。",
                                  "../assets/offroad/icon_mute.png",
                                  this));

  toggles.append(new ParamControl("OnroadScreenOff",
                                  "上路關閉螢幕",
                                  "設備連線後將會關閉螢幕，用以延長 OLED 螢幕的壽命，當觸碰螢幕時會重新開啟螢幕以進行操作設定。可以在汽車啟動時切換此功能。",
                                  "../assets/offroad/icon_metric.png",
                                  this));

  toggles.append(new ParamControl("TurnVisionControl",
                                  "彎道減速",
                                  "使用視覺路徑預測來估算適當的速度，可自動降速以順利通過前方的轉彎。",
                                  "../assets/offroad/icon_road.png",
                                  this));

  toggles.append(new ParamControl("LQR",
                                  "啟用 LQR 控制 (PRIUS ALPHA 專用)",
                                  "開啟 LQR 操控模式，用以取代預設的 Torque 操控模式。 (PRIUS ALPHA專用)",
                                  "../assets/offroad/icon_road.png",
                                  this));

  toggles.append(new ParamControl("UploadRaw",
                                  "上傳 Raw 記錄",
                                  "當連接Wi-Fi網路時，可自動上傳完整的行車記錄檔及完整解晰度行車錄像。如果未開啟，則只會上傳個別的行車記錄到useradmin.comma.ai.",
                                  "../assets/offroad/icon_network.png",
                                  this));

  for (ParamControl *toggle : toggles) {
    if (main_layout->count() != 0) {
      toggle_layout->addWidget(horizontal_line());
    }
    toggle_layout->addWidget(toggle);
  }
}