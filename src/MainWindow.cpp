#include "MainWindow.h"

#include <QAudioOutput>
#include <QBoxLayout>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QFrame>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMediaPlayer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QShortcut>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QGraphicsDropShadowEffect>

namespace {
constexpr int kSeekSliderRange = 1000;
constexpr int kFilePathRole = Qt::UserRole + 1;
constexpr int kSearchRole = Qt::UserRole + 2;

QString normalizeText(QString text) {
    text = text.toLower();
    text.replace('_', ' ');
    text.replace('-', ' ');
    text.replace(QRegularExpression("\\s+"), " ");
    return text.simplified();
}

class TrackFilterProxy final : public QSortFilterProxyModel {
public:
    explicit TrackFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {
        setDynamicSortFilter(true);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    void setFilterText(const QString &text) {
        filterText_ = normalizeText(text);
        tokens_ = filterText_.split(' ', Qt::SkipEmptyParts);
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        if (tokens_.isEmpty()) return true;
        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString key = index.data(kSearchRole).toString();
        if (key.isEmpty()) return true;
        for (const QString &token : tokens_) {
            if (!key.contains(token)) return false;
        }
        return true;
    }

private:
    QString filterText_;
    QStringList tokens_;
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUi();

    model_ = new QStandardItemModel(this);
    auto *proxy = new TrackFilterProxy(this);
    filter_ = proxy;
    filter_->setSourceModel(model_);
    filter_->sort(0);
    listView_->setModel(filter_);

    audioOutput_ = new QAudioOutput(this);
    player_ = new QMediaPlayer(this);
    player_->setAudioOutput(audioOutput_);
    audioOutput_->setVolume(0.7);

    // Signals
    connect(addFolderButton_, &QToolButton::clicked, this, &MainWindow::addFolder);
    connect(playPauseButton_, &QPushButton::clicked, this, &MainWindow::playPause);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stop);
    connect(prevButton_, &QToolButton::clicked, this, &MainWindow::playPrevious);
    connect(nextButton_, &QToolButton::clicked, this, &MainWindow::playNext);
    connect(shuffleButton_, &QToolButton::clicked, this, &MainWindow::toggleShuffle);
    connect(repeatButton_, &QToolButton::clicked, this, &MainWindow::cycleRepeat);
    connect(listView_, &QListView::doubleClicked, this, &MainWindow::playSelected);
    connect(searchEdit_, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(player_, &QMediaPlayer::positionChanged, this, &MainWindow::updatePosition);
    connect(player_, &QMediaPlayer::durationChanged, this, &MainWindow::updateDuration);
    connect(player_, &QMediaPlayer::playbackStateChanged, this, &MainWindow::updatePlayState);
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, &MainWindow::handleMediaStatus);
    connect(seekSlider_, &QSlider::valueChanged, this, &MainWindow::seek);
    connect(volumeSlider_, &QSlider::valueChanged, this, &MainWindow::updateVolume);
    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::updateSelectionLabel);

    // Shortcuts
    new QShortcut(QKeySequence(Qt::Key_Space), this, SLOT(playPause()));
    new QShortcut(QKeySequence::Find, this, [this]() { searchEdit_->setFocus(); searchEdit_->selectAll(); });

    // Initial Scan
    const QString musicDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (!musicDir.isEmpty()) scanFolder(musicDir);
    updateCounts();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    auto *central = new QWidget(this);
    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- LEFT SIDEBAR ---
    auto *sidebar = new QFrame(central);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(280);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(24, 40, 24, 24);
    sidebarLayout->setSpacing(20);

    auto *logoLabel = new QLabel("MusicBlue", sidebar);
    logoLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #4FB6FF; margin-bottom: 10px;");
    
    searchEdit_ = new QLineEdit(sidebar);
    searchEdit_->setPlaceholderText("楽曲を検索...");
    searchEdit_->setClearButtonEnabled(true);

    addFolderButton_ = new QToolButton(sidebar);
    addFolderButton_->setText(" フォルダを追加");
    addFolderButton_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    addFolderButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addFolderButton_->setObjectName("accentButton");
    addFolderButton_->setFixedHeight(45);
    addFolderButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    countLabel_ = new QLabel(sidebar);
    countLabel_->setObjectName("countLabel");

    sidebarLayout->addWidget(logoLabel);
    sidebarLayout->addWidget(new QLabel("LIBRARY", sidebar));
    sidebarLayout->addWidget(searchEdit_);
    sidebarLayout->addSpacing(10);
    sidebarLayout->addWidget(addFolderButton_);
    sidebarLayout->addStretch();
    sidebarLayout->addWidget(countLabel_);

    // --- RIGHT CONTENT ---
    auto *contentArea = new QWidget(central);
    auto *contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(30, 30, 30, 30);
    contentLayout->setSpacing(20);

    auto *listHeader = new QLabel("すべての楽曲", contentArea);
    listHeader->setStyleSheet("font-size: 18px; font-weight: bold; color: #1F4B6E;");

    listView_ = new QListView(contentArea);
    listView_->setFrameShape(QFrame::NoFrame);
    listView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listView_->setUniformItemSizes(true);

    // --- BOTTOM PLAYER PANEL ---
    auto *playerPanel = new QFrame(contentArea);
    playerPanel->setObjectName("playerPanel");
    playerPanel->setFixedHeight(130);
    auto *playerLayout = new QVBoxLayout(playerPanel);
    playerLayout->setContentsMargins(20, 10, 20, 15);

    seekSlider_ = new QSlider(Qt::Horizontal, playerPanel);
    seekSlider_->setObjectName("seekSlider");
    seekSlider_->setEnabled(false);

    auto *ctrlRow = new QHBoxLayout();
    
    // Track Info
    auto *infoBox = new QVBoxLayout();
    nowPlayingTitleLabel_ = new QLabel("楽曲が選択されていません", playerPanel);
    nowPlayingTitleLabel_->setStyleSheet("font-weight: bold; color: #1F4B6E; font-size: 14px;");
    nowPlayingPathLabel_ = new QLabel("---", playerPanel);
    nowPlayingPathLabel_->setStyleSheet("color: #6FBBE6; font-size: 12px;");
    infoBox->addWidget(nowPlayingTitleLabel_);
    infoBox->addWidget(nowPlayingPathLabel_);

    // Buttons
    auto *btnBox = new QHBoxLayout();
    btnBox->setSpacing(15);
    prevButton_ = new QToolButton(playerPanel);
    prevButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    
    playPauseButton_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "", playerPanel);
    playPauseButton_->setObjectName("playPauseButton");
    playPauseButton_->setFixedSize(50, 50);

    stopButton_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), "", playerPanel);
    stopButton_->setObjectName("stopButton");
    stopButton_->setFixedSize(36, 36);

    nextButton_ = new QToolButton(playerPanel);
    nextButton_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));

    btnBox->addWidget(prevButton_);
    btnBox->addWidget(playPauseButton_);
    btnBox->addWidget(stopButton_);
    btnBox->addWidget(nextButton_);

    // Misc Controls
    auto *miscBox = new QHBoxLayout();
    timeLabel_ = new QLabel("00:00 / 00:00", playerPanel);
    shuffleButton_ = new QToolButton(playerPanel);
    shuffleButton_->setText("S");
    shuffleButton_->setCheckable(true);
    repeatButton_ = new QToolButton(playerPanel);
    repeatButton_->setText("R");
    
    volumeSlider_ = new QSlider(Qt::Horizontal, playerPanel);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(70);
    volumeSlider_->setFixedWidth(100);

    miscBox->addWidget(shuffleButton_);
    miscBox->addWidget(repeatButton_);
    miscBox->addSpacing(10);
    miscBox->addWidget(timeLabel_);
    miscBox->addWidget(volumeSlider_);

    ctrlRow->addLayout(infoBox, 2);
    ctrlRow->addStretch(1);
    ctrlRow->addLayout(btnBox);
    ctrlRow->addStretch(1);
    ctrlRow->addLayout(miscBox);

    playerLayout->addWidget(seekSlider_);
    playerLayout->addLayout(ctrlRow);

    contentLayout->addWidget(listHeader);
    contentLayout->addWidget(listView_, 1);
    contentLayout->addWidget(playerPanel);

    mainLayout->addWidget(sidebar);
    mainLayout->addWidget(contentArea, 1);

    setCentralWidget(central);
    setWindowTitle("MusicBlue Player");
    resize(1150, 800);

    // --- STYLESHEET ---
    setStyleSheet(
        "QMainWindow { background-color: #F7FBFF; }"
        "#sidebar { background-color: #F1F8FF; border-right: 1px solid #DDEEFF; }"
        "#sidebar QLabel { color: #5AA9D6; font-weight: bold; font-size: 11px; }"
        
        "QLineEdit { background-color: #FFFFFF; border: 1px solid #CFE6FF; border-radius: 10px; padding: 10px; color: #1F4B6E; }"
        "QLineEdit:focus { border: 1px solid #4FB6FF; }"
        
        "QListView { background-color: transparent; outline: none; }"
        "QListView::item { padding: 15px; border-bottom: 1px solid #ECF5FF; color: #2E5A7A; border-radius: 8px; margin-bottom: 2px; }"
        "QListView::item:hover { background-color: #EAF5FF; }"
        "QListView::item:selected { background-color: #4FB6FF; color: #FFFFFF; }"
        
        "#accentButton { background-color: #4FB6FF; color: white; border-radius: 12px; font-weight: bold; font-size: 13px; }"
        "#accentButton:hover { background-color: #36A6F5; }"
        
        "#playerPanel { background-color: #FFFFFF; border: 1px solid #DDEEFF; border-radius: 20px; }"
        
        "#playPauseButton { background-color: #4FB6FF; border-radius: 25px; color: white; }"
        "#playPauseButton:hover { background-color: #36A6F5; }"
        
        "#stopButton { background-color: #EEF6FF; border-radius: 18px; }"
        
        "QSlider::groove:horizontal { height: 6px; background: #DDEEFF; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -4px 0; background: #4FB6FF; border-radius: 7px; }"
        "#seekSlider::sub-page:horizontal { background: #4FB6FF; border-radius: 3px; }"
        
        "QToolButton { border: none; color: #5AA9D6; font-weight: bold; }"
        "QToolButton:hover { color: #4FB6FF; }"
    );
}

// --- Logic Methods (Provided by user, maintained) ---

void MainWindow::addFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Select music folder");
    if (!dir.isEmpty()) { scanFolder(dir); updateCounts(); }
}

void MainWindow::scanFolder(const QString &path) {
    static const QStringList kFilters = {"*.mp3", "*.flac", "*.wav", "*.ogg", "*.m4a", "*.aac"};
    QDirIterator it(path, kFilters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) addTrack(it.next());
    filter_->sort(0);
}

void MainWindow::addTrack(const QString &filePath) {
    if (trackSet_.contains(filePath)) return;
    trackSet_.insert(filePath);
    const QFileInfo info(filePath);
    auto *item = new QStandardItem(info.completeBaseName());
    item->setData(filePath, kFilePathRole);
    const QString searchKey = normalizeText(info.completeBaseName() + " " + info.fileName() + " " + info.absolutePath());
    item->setData(searchKey, kSearchRole);
    model_->appendRow(item);
}

void MainWindow::playSelected() { playIndex(listView_->currentIndex()); }

void MainWindow::playIndex(const QModelIndex &proxyIndex) {
    if (!proxyIndex.isValid()) return;
    playTrack(filter_->mapToSource(proxyIndex).data(kFilePathRole).toString());
}

void MainWindow::playTrack(const QString &filePath, bool recordHistory) {
    if (filePath.isEmpty()) return;
    player_->setSource(QUrl::fromLocalFile(filePath));
    player_->play();
    currentFilePath_ = filePath;
    const QFileInfo info(filePath);
    nowPlayingTitleLabel_->setText(info.completeBaseName());
    nowPlayingPathLabel_->setText(info.absolutePath());
    if (recordHistory && (playHistory_.isEmpty() || playHistory_.last() != filePath)) playHistory_.append(filePath);

    for (int row = 0; row < filter_->rowCount(); ++row) {
        if (filter_->mapToSource(filter_->index(row, 0)).data(kFilePathRole).toString() == filePath) {
            listView_->setCurrentIndex(filter_->index(row, 0));
            break;
        }
    }
}

void MainWindow::playPause() {
    if (player_->playbackState() == QMediaPlayer::PlayingState) player_->pause();
    else if (!player_->source().isEmpty()) player_->play();
    else if (listView_->currentIndex().isValid()) playSelected();
    else if (filter_->rowCount() > 0) playIndex(filter_->index(0, 0));
}

void MainWindow::playNext() {
    int total = filter_->rowCount();
    if (total == 0) return;
    int nextRow = (shuffleEnabled_) ? QRandomGenerator::global()->bounded(total) : listView_->currentIndex().row() + 1;
    if (nextRow >= total) nextRow = (repeatMode_ == 1) ? 0 : -1;
    if (nextRow != -1) playIndex(filter_->index(nextRow, 0));
}

void MainWindow::playPrevious() {
    if (playHistory_.size() >= 2) { playHistory_.removeLast(); playTrack(playHistory_.last(), false); return; }
    int prevRow = listView_->currentIndex().row() - 1;
    if (prevRow < 0) prevRow = (repeatMode_ == 1) ? filter_->rowCount() - 1 : -1;
    if (prevRow != -1) playIndex(filter_->index(prevRow, 0));
}

void MainWindow::stop() { player_->stop(); }

void MainWindow::updatePosition(qint64 position) {
    if (durationMs_ > 0) {
        seekSlider_->blockSignals(true);
        seekSlider_->setValue(static_cast<int>((position * kSeekSliderRange) / durationMs_));
        seekSlider_->blockSignals(false);
    }
    timeLabel_->setText(QString("%1 / %2").arg(formatTime(position), formatTime(durationMs_)));
}

void MainWindow::updateDuration(qint64 duration) {
    durationMs_ = duration;
    seekSlider_->setEnabled(durationMs_ > 0);
}

void MainWindow::seek(int value) {
    if (durationMs_ > 0) player_->setPosition((durationMs_ * value) / kSeekSliderRange);
}

void MainWindow::onSearchTextChanged(const QString &text) {
    static_cast<TrackFilterProxy *>(filter_)->setFilterText(text);
    updateCounts();
}

void MainWindow::updatePlayState() {
    bool playing = (player_->playbackState() == QMediaPlayer::PlayingState);
    playPauseButton_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void MainWindow::updateSelectionLabel(const QModelIndex &current) {
    if (!current.isValid() || player_->playbackState() == QMediaPlayer::PlayingState) return;
    const QFileInfo info(filter_->mapToSource(current).data(kFilePathRole).toString());
    nowPlayingTitleLabel_->setText(info.completeBaseName());
    nowPlayingPathLabel_->setText(info.absolutePath());
}

void MainWindow::handleMediaStatus(QMediaPlayer::MediaStatus status) {
    if (status == QMediaPlayer::EndOfMedia) {
        if (repeatMode_ == 2) playTrack(currentFilePath_, false);
        else playNext();
    }
}

void MainWindow::toggleShuffle() { shuffleEnabled_ = !shuffleEnabled_; }
void MainWindow::cycleRepeat() { 
    repeatMode_ = (repeatMode_ + 1) % 3; 
    repeatButton_->setText(repeatMode_ == 0 ? "Off" : (repeatMode_ == 1 ? "All" : "One")); 
}
void MainWindow::updateVolume(int value) { audioOutput_->setVolume(value / 100.0f); }
void MainWindow::updateCounts() {
    countLabel_->setText(QString("%1 Tracks found").arg(model_->rowCount()));
}
QString MainWindow::formatTime(qint64 ms) const {
    qint64 s = ms / 1000;
    return QString("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}
