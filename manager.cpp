#include "manager.h"

/**
  callback from sqlite to set the watch_dir
  TODO make inline
 */
static int c_set_watch_dir(void *, int , char **argv, char **) {
  manager::watch_dir = string(argv[0]);
  return 1;
}

namespace manager{

  // set of statements to execute on startup
  // TODO move to own file
  string db_MIGRATE[3] =
  {
          "CREATE TABLE IF NOT EXISTS tracks("  \
          "title TEXT NOT NULL," \
          "title_search TEXT NOT NULL," \
          "album TEXT," \
          "album_search TEXT," \
          "artist TEXT," \
          "artist_search TEXT," \
          "comment TEXT," \
          "genre TEXT," \
          "year INT," \
          "track INT," \
          "duration INT," \
          "file_path TEXT UNIQUE" \
          ");"
          ,
          "CREATE TABLE IF NOT EXISTS tracks_user_data(" \
          "track_id INT UNIQUE," \
          "playcount INT DEFAULT 0," \
          "rating INT DEFAULT 0" \
          ");"
          ,
          "CREATE TABLE IF NOT EXISTS config_vars(" \
          "key TEXT PRIMARY KEY," \
          "value TEXT" \
          ");"
  };

  sqlite3 *db = nullptr;
  // absloute location of db file
  string db_file_name = "/home/karthik/.config/playa/storage.db3";
  string watch_dir;

  /**
    startup me up
   */
  void manager_init() {

    mkdir("/home/karthik/.config",S_IRWXU | S_IRWXG);
    mkdir("/home/karthik/.config/playa",S_IRWXU | S_IRWXG);

    // create the sqlite db if it doesnt exist
    if(sqlite3_open(db_file_name.c_str(), &db))
      printf("error opening db file :: %s \n",sqlite3_errmsg(db));

    // execute db statemens
    for(string sql: db_MIGRATE)
      db_EXECUTE(sql);

    db_EXECUTE("SELECT value FROM config_vars WHERE key = 'watch_dir';",c_set_watch_dir,0);

  }

  /**
    scan a directory for files
    a callback on finding a file
   */
  void get_new_media_files(){
    if (nftw(watch_dir.c_str(), manager::get_file_info, 20, FTW_DEPTH|FTW_PHYS)
        == -1) {
      printf("some error =======/n ");
    }
  }

  /**
    check if a file is a valid audio file and insert into db
   */
  int get_file_info(const char *fpath, const struct stat *,
                    int tflag, struct FTW *) {
    // make sure its a file
    if (tflag != FTW_F)
      return 0;

    string allowed_extensions[] = {"mp3","m4a","wma","flac"};

    string tr_title, tr_artist, tr_album,
                    tr_comment, tr_genre,
                    tr_year, tr_track,
                    tr_title_searchable, tr_album_searchable, tr_artist_searchable;

    // get the extension
    string file_ext = string(fpath).substr(string(fpath).find_last_of(".") + 1);

    // convert the extension to lower case
    transform(file_ext.begin(), file_ext.end(), file_ext.begin(), ::tolower);

    // check if the extension belogns to allowed_extensions
    if(find(begin(allowed_extensions), end(allowed_extensions), file_ext)
       != end(allowed_extensions)){

      // taglib it
      TagLib::FileRef f(fpath);

      // invalid audio file?
      if(!f.file()->isValid())
        return 0;

      // set for insertion into the db
      tr_title = sanitize_track_data(f.tag()->title(),fpath);
      tr_album = sanitize_track_data(f.tag()->album(),"");
      tr_artist = sanitize_track_data(f.tag()->artist(),"");
      tr_comment = sanitize_track_data(f.tag()->comment(),"");
      tr_genre = sanitize_track_data(f.tag()->genre(),"");
      tr_year = sanitize_track_data(std::to_string(f.tag()->year()),"");
      tr_track = sanitize_track_data(std::to_string(f.tag()->track()),"");

      // might use, not sure
      tr_title_searchable = search_friendly(tr_title);
      tr_album_searchable = search_friendly(tr_album);
      tr_artist_searchable = search_friendly(tr_artist);

      // insert into tracks
      if(db_EXECUTE("INSERT INTO " \
                    "tracks(title,title_search,album,album_search,artist,artist_search,comment,genre,year,track,duration,file_path) VALUES(" \
                    "'"+ tr_title + "'," \
                    "'"+ tr_title_searchable + "'," \
                    "'"+ tr_album + "'," \
                    "'"+ tr_album_searchable + "'," \
                    "'"+ tr_artist +"'," \
                    "'"+ tr_artist_searchable +"'," \
                    "'"+ tr_comment +"'," \
                    "'"+ tr_genre +"',"
                    + tr_year+"," \
                    + tr_track+"," \
                    + std::to_string(f.audioProperties()->length())+"," \
                    "'"+fpath+"'"
                    ")") == 0)
        printf("commit to tracks failed, %s \n ", sqlite3_errmsg(db));
      // insert into tracks_user_data
      if(db_EXECUTE("INSERT INTO " \
                    "tracks_user_data(track_id) VALUES(" \
                    +std::to_string(sqlite3_last_insert_rowid(db))+")")
         == 0)
        printf("commit to tracks_user_data failed, %s \n ", sqlite3_errmsg(db));
    }
    return 0;
  }

  /**
    wrapper for just executing a sql statement no callback etc
   */
  int db_EXECUTE(string sql){
    return db_EXECUTE(sql,0,0);
  }

  /**
    wrapper for executing a sql statement w/ callback
   */
  int db_EXECUTE(string sql,int (*callback)(void*,int,char**,char**), void *data){
    char *_errMsg = 0;
    int rc = 0;
    sqlite3_exec(db, sql.c_str(), callback, data, &_errMsg);
    if( rc != SQLITE_OK ){
      printf(" ============ SQL error: %s =========== \n", _errMsg);
      sqlite3_free(_errMsg);
      return 0;
    }
    return 1;
  }

  /**
    make it sql friendly string
   */
  string sanitize_track_data(TagLib::String taglib_data,string alt){

    string data = ((taglib_data == TagLib::String::null)
                   ? alt : taglib_data.to8Bit(true));
    if(data.empty())
      return alt;

    string final_str;

    int i = -1;
    // escape quotes and slash
    while(data[++i]){
      if(data[i] == '\\' || data[i] == '\'')
        final_str += '\\';
      final_str += data[i];
    }

    return final_str;
  }

  /**
    make it viable for search
   */
  string search_friendly(string data){
    // lowercase
    transform(data.begin(), data.end(), data.begin(), ::tolower);

    string final_str;
    int i=-1;
    // remove non alphabets
    while(data[++i])
      if(isalpha(data[i]))
        final_str += data[i];

    return final_str;
  }

  /**
    poweroff
   */
  void manager_shutdown(){
    sqlite3_close(db);
  }
}
