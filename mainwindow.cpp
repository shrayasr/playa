#include "mainwindow.h"
#include "ui_mainwindow.h"

/**
  sqlite3_exec callback creates a media_data dict,
  passed to new_media_to_list to add it to qt
 */
static int c_new_media_to_list(void *param, int argc, char **argv, char **azColName)
{
  MainWindow* mw = reinterpret_cast<MainWindow*>(param);
  map<string, string> media_data;
  for(int i=0; i<argc; i++){
    //printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    media_data[azColName[i]] = argv[i] ? argv[i] : NULL;
  }
  mw->new_media_to_list(media_data);
  return 0;
}

/**
  does the ui setup
 */
MainWindow::MainWindow(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::MainWindow){
  ui->setupUi(this);
  using namespace std::placeholders;

  // init stuff
  manager::manager_init();
  player::player_init();
  lastfm_helper::lastfm_helper_init();

  // register callbacks
  lastfm_helper::on_user_should_auth =
                  std::bind(&MainWindow::open_browser_to_auth,this,_1);

  //    std::async(std::launch::async,manager::get_new_media_files);
  player::time_changed = std::bind(&MainWindow::on_time_changed, this, _1);
  player::play_toggled = std::bind(&MainWindow::on_play_toggled, this, _1);
  player::end_reached = std::bind(&MainWindow::on_end_reached, this, _1);
  player::media_changed = std::bind(&MainWindow::on_media_changed, this, _1);
  player::reset_gui = std::bind(&MainWindow::on_reset_gui, this, _1);

  // number of columns for the mediatable
  ui->media_item_tableWidget->setColumnCount(3);

  // column headers
  ui->media_item_tableWidget->setHorizontalHeaderItem(
                          0,new QTableWidgetItem(QString("Title"),QTableWidgetItem::Type));
  ui->media_item_tableWidget->setHorizontalHeaderItem(
                          1,new QTableWidgetItem(QString("Artist"),QTableWidgetItem::Type));
  ui->media_item_tableWidget->setHorizontalHeaderItem(
                          2,new QTableWidgetItem(QString("Album"),QTableWidgetItem::Type));

  // get the media files and add it to the table
  manager::db_EXECUTE("SELECT title,album,artist,t.rowid FROM tracks AS t;"
                      ,c_new_media_to_list,this);
  ui->media_item_tableWidget->resizeColumnsToContents();

  // get the default values of ui elements (i.e when they are empty)
  title_lbl_def = ui->title_lbl->text();
  artist_lbl_def = ui->artist_lbl->text();
}

/**
  close it all
 */
MainWindow::~MainWindow(){
  delete ui;
  manager::manager_shutdown();
  player::player_shutdown();
}

/**
  called when the next track is needed or when current track ends
  if playlist is shuffled the obtain a random item_id
  move to player?/manager?
 */
void MainWindow::find_next_track(int){
  int selected_row_id = -1;

  if (ui->shuffle_bt->isChecked()){
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 eng(rd()); // seed the generator
    std::uniform_int_distribution<> distr(
                            1, ui->media_item_tableWidget->rowCount()); // the range of the random number

    // make sure its not the same rowid
    do { selected_row_id = distr(eng); }
    while(current_item_row_id == selected_row_id);
  }
  else
    selected_row_id = current_item_row_id + 1;

  // trigger a double click on the item
  on_media_item_tableWidget_itemDoubleClicked(
                          ui->media_item_tableWidget->item(selected_row_id,0));
}

// EVENTS

/**
  when a new item is added add it to the mediatable
 */
void MainWindow::new_media_to_list(map<string, string> media_data){
  int row_no = ui->media_item_tableWidget->rowCount();
  string thumbs_rating = "";
  ui->media_item_tableWidget->insertRow(row_no);

  // set the title and add set the sqlite rowid as the data
  // this rowid is later used to fetch the respecitve fiel_url in the backend
  QTableWidgetItem *title_itm = new QTableWidgetItem(media_data["title"].c_str());
  title_itm->setData(Qt::UserRole,media_data["rowid"].c_str());
  ui->media_item_tableWidget->setItem(row_no,0,title_itm);

  ui->media_item_tableWidget->setItem(
                          row_no,1,new QTableWidgetItem(media_data["artist"].c_str()));
  ui->media_item_tableWidget->setItem(
                          row_no,2,new QTableWidgetItem(media_data["album"].c_str()));
}

/**
  last.fm
  open a url in the browser for the user to authenticate
 */
void MainWindow::open_browser_to_auth(string Url){
  QDesktopServices::openUrl(QUrl(Url.c_str()));
}

// player EVENTS

/**
  when the current_time changes
  vlc gives us a value b/w 0 and 1
 */
void MainWindow::on_time_changed(float time){
  // vlc gives a precision of 6 digits, hence the multiply
  if(!slider_pressed)
    ui->time_slider->setValue(ui->time_slider->maximum() * time);

  // also update the duration
  ui->time_done->setText(
                          QString::fromStdString(player::to_duration(
                                                         atoi(player::track_data["duration"].c_str()) * time)));
}

/**
  when play/pause is toggled, set the play_bt text accordingly
 */
void MainWindow::on_play_toggled(int play_state){
  // if playing set the text to pause
  if(play_state == 1){
    ui->play_bt->setText("Pause");
  }
  else{
    ui->play_bt->setText("Play");
  }
}

/**
  when the current track ends
  reset the labels, and get next track to play
  these labels are again set later when playing starts
 */
void MainWindow::on_end_reached(int){
  on_reset_gui(0);
  find_next_track(-1);
}

/**
  when a new track is set
  set the lables
 */
void MainWindow::on_media_changed(map<string,string> track_data){
  ui->title_lbl->setText(QString(track_data["title"].c_str()));
  if(track_data["artist"] != "")
    ui->artist_lbl->setText(QString(track_data["artist"].c_str()));
  else
    ui->artist_lbl->setText(artist_lbl_def);

  ui->up_bt->setEnabled(true);
  if(track_data["rating"] == "5")
    ui->up_bt->setChecked(true);

  ui->time_total->setText(QString::fromStdString(player::to_duration(atoi(track_data["duration"].c_str()))));
}

/**
  when the a request to reset the gui comes in
 */
void MainWindow::on_reset_gui(int){
  ui->time_done->setText("--");
  ui->time_total->setText("--");

  ui->up_bt->setEnabled(false);
  ui->up_bt->setChecked(false);

  ui->title_lbl->setText(title_lbl_def);
  ui->artist_lbl->setText(artist_lbl_def);
}
// player EVENTS END


// SLOTS

/**
  qt media table
  when an item is dbclicked
 */
void MainWindow::on_media_item_tableWidget_itemDoubleClicked(QTableWidgetItem *item)
{
  // get the rowid stored in the title, play the track
  current_item_row_id = item->row();

  previous_row_id = current_row_id; // we'll use this when going to previously played track
  current_row_id = item->data(Qt::UserRole).toInt();
  player::set_media(current_row_id);
}

/**
  when the value on the slider is changed
  check to ensure its not being manually dragged
 */
void MainWindow::on_time_slider_valueChanged(int value){
  if(slider_pressed)
    new_seek_value = value;
}

/**
  when the slider is being dragged
 */
void MainWindow::on_time_slider_sliderPressed(){
  slider_pressed = true;
}

/**
  when the slider is released seek_to the position
 */
void MainWindow::on_time_slider_sliderReleased(){
  slider_pressed = false;
  player::seek_to((float)new_seek_value/(float)ui->time_slider->maximum());
}

/**
  when play/puse is clicked
 */
void MainWindow::on_play_bt_clicked(){
  if (player::is_playing == 1)
    player::pause();
  else
    player::play();
}

/**
  when prev bt is clicked
 */
void MainWindow::on_prev_bt_clicked(){
  player::set_media(previous_row_id); // previous -> previous plays the same track again, to fix or not to fix ?
}

/**
  when next bt is clicked
 */
void MainWindow::on_next_bt_clicked(){
  find_next_track(-1);
}

/**
  when settings bt is clicked
  shows the setting dialog
 */
void MainWindow::on_settings_bt_clicked(){
  settings = new settingswindow(this);
  settings->show();
}

/**
  when the track_up is toggled
  call a func in the player
 */
void MainWindow::on_up_bt_released(){

  if (ui->up_bt->isChecked())
    player::track_up(true);
  else
    player::track_up(false);

}
// SLOTS END

