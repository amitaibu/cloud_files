<?php
/**
 * @file
 *
 * Implement DrupalStreamWrapperInterface to provide an rcf:// streamwrapper.
 */
class RackspaceCloudFilesStreamWrapper implements DrupalStreamWrapperInterface {
  /**
   * Instance URI (stream).
   *
   * These streams will be referenced as 'rcf://target'
   *
   * @var String
   */
  protected $uri;

  /**
   * A generic resource handle.
   */
  public $handle = NULL;

  /**
   * Pointer to where we are in a directory read.
   */
  protected $directory_pointer = 0;

  /**
   * List of files in a given directory.
   */
  protected $directory_list;

  /**
   * The pointer to the next read or write
   */
  protected $stream_pointer = 0;

  /**
   * A buffer for reading/wrting.
   */
  protected $stream_data = NULL;

  /**
   * Object cache
   *
   * @var CFResponse
   */
  protected $object = NULL;

  /**
   * Container cache
   */
  protected $container = NULL;

  /**
   * The Cloud files container entity.
   *
   * @var CloudFilesContainer
   */
  protected $cloud_files_container = NULL;

  /**
   * Data is not written to rackspace in stream_write to minimize requests
   * to rackspace. instead, data is written to the $stream_data property.
   * This $write property is flagged as true, and in stream_flush, all the
   * data is written to rackspace at once.
   *
   * @var <type> Boolean
   */
  protected $write = FALSE;

  /**
   * Implements setUri().
   */
  function setUri($uri) {
    $this->uri = $uri;
  }

  /**
   * Implements getUri().
   */
  function getUri() {
    return $this->uri;
  }

  /**
   * Returns the local writable target of the resource within the stream.
   *
   * @param $uri
   *   Optional URI.
   *
   * @return
   *   Returns a string representing a location suitable for writing of a file,
   *   or FALSE if unable to write to the file such as with read-only streams.
   */
  protected function getTarget($uri = NULL) {
    if (!isset($uri)) {
      $uri = $this->uri;
    }

    list($scheme, $target) = explode('://', $uri, 2);

    // Remove erroneous leading or trailing, forward-slashes and backslashes.
    return trim($target, '\/');
  }

  /**
   * Base implementation of getMimeType().
   */
  static function getMimeType($uri, $mapping = NULL) {
    if (!isset($mapping)) {
      // The default file map, defined in file.mimetypes.inc is quite big.
      // We only load it when necessary.
      include_once DRUPAL_ROOT . '/includes/file.mimetypes.inc';
      $mapping = file_mimetype_mapping();
    }

    $extension = '';
    $file_parts = explode('.', basename($uri));

    // Remove the first part: a full filename should not match an extension.
    array_shift($file_parts);

    // Iterate over the file parts, trying to find a match.
    // For my.awesome.image.jpeg, we try:
    //   - jpeg
    //   - image.jpeg, and
    //   - awesome.image.jpeg
    while ($additional_part = array_pop($file_parts)) {
      $extension = strtolower($additional_part . ($extension ? '.' . $extension : ''));
      if (isset($mapping['extensions'][$extension])) {
        return $mapping['mimetypes'][$mapping['extensions'][$extension]];
      }
    }

    return 'application/octet-stream';
  }

  /**
   * Overrides getExternalUrl().
   */
  function getExternalUrl() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing getExternalUrl (%uri)", array('%uri' => $this->uri), WATCHDOG_DEBUG);
    }

    $target = $this->getLocalPath();
    $path = explode('/', $target);

    $uri = NULL;
    // if its a request for styles, need to check if it exists
    if ($path[0] == 'styles') {
      // Get the original URI.
      $style_name = $path[1];
      unset($path[0], $path[1], $path[2]);
      $uri = 'rcf://' . implode('/', $path);

      if (!isset($this->object) && !cloud_files_set_image_style_by_uri($uri, $style_name)) {
        //exceptions are expensive, but unfortunately RS doesnt provide a
        // better way to check if a file exists.
        try {
          $this->getContainer($uri);
          // Get the original container.
          $this->object = $this->container->get_object($target);
        }
        catch (NoSuchObjectException $e) {
          // TODO: Require lock.
          $style = image_style_load($style_name);
          image_style_create_derivative($style, $uri, $this->uri);
        }
      }
    }

    $this->getCloudFilesContainer($uri);
    $container = $this->cloud_files_container;

    if (!$container->published) {
      if ($key = variable_get('rackspace_cloud_temp_url_key')) {
        try {
          if (!$this->object) {
            $this->getContainer($uri);
            $this->object = $this->container->get_object($target);
          }
          // TODO: Change to variable.
          $expires = $container->temp_url_expire ? $container->temp_url_expire : 60;
          // TODO: Allow define HTTP method to PUT as-well?
          return $this->object->get_temp_url($key, $expires, 'GET');
        }
        catch (NoSuchObjectException $e) {
          return;
        }
      }
    }
    else {
      return 'http://' . $this->cloud_files_container->cdn_domain . '/' . $target;
    }
  }

  /**
   * We have no concept of chmod, so just return TRUE.
   */
  function chmod($mode) {
    //TODO chmod
    return TRUE;
  }

  /**
   * Implements realpath().
   */
  function realpath() {
    return 'rcf://' . $this->getLocalPath();
  }

  protected function getLocalPath($uri = NULL) {
    if (!isset($uri)) {
      $uri = $this->uri;
    }
    return $this->getTarget($uri);
  }

  /**
   * Opens a stream, as for fopen(), file_get_contents(), file_put_contents()
   *
   * @param $uri
   *   A string containing the URI to the file to open.
   * @param $mode
   *   The file mode ("r", "wb" etc.).
   * @param $options
   *   A bit mask of STREAM_USE_PATH and STREAM_REPORT_ERRORS.
   * @param &$opened_path
   *   A string containing the path actually opened.
   *
   * @return
   *   Returns TRUE if file was opened successfully. (Always returns TRUE).
   *
   * @see http://php.net/manual/en/streamwrapper.stream-open.php
   */
  public function stream_open($uri, $mode, $options, &$opened_path) {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing stream_open (%uri)", array('%uri' => $uri), WATCHDOG_DEBUG);
    }

    //reset data
    $this->stream_pointer = 0;
    $this->stream_data = NULL;

    $this->uri = $uri;
    $target = $this->getLocalPath();

    $uri = NULL;
    $path = explode('/', $target);
    if ($path[0] == 'styles') {
      // Get the container according to the original file.
      unset($path[0], $path[1], $path[2]);
      $uri = 'rcf://' . implode('/', $path);
    }

    $this->getContainer($uri);

    //this will load the object if it exists
    $this->object = $this->container->create_object($target);

    //TODO check $mode, $options and act accordingly
    return TRUE;
  }

  /**
   * Support for flock().
   *
   * @param $operation
   *   One of the following:
   *   - LOCK_SH to acquire a shared lock (reader).
   *   - LOCK_EX to acquire an exclusive lock (writer).
   *   - LOCK_UN to release a lock (shared or exclusive).
   *   - LOCK_NB if you don't want flock() to block while locking (not
   *     supported on Windows).
   *
   * @return
   *   Always returns TRUE at the present time. (no support)
   *
   * @see http://php.net/manual/en/streamwrapper.stream-lock.php
   */
  public function stream_lock($operation) {
    //Rackspace Cloud Files does not have locking
    return False;
  }

  /**
   * Support for fread(), file_get_contents() etc.
   *
   * @param $count
   *   Maximum number of bytes to be read.
   *
   * @return
   *   The string that was read, or FALSE in case of an error.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-read.php
   */
  public function stream_read($count) {
    //if the data is empty, get the data from the object
    //this stores all the data in the $stream_data property, only request once
    if (!$this->stream_data) {
      //if the object is empty, get the object from rackspace
      if (!$this->object) {
        $this->getContainer();
        $this->object = $this->container->get_object($target);
      }
      $this->stream_data = $this->object->read();
    }
    $data = substr($this->stream_data, $this->stream_pointer, $count);
    $this->stream_pointer += $count;
    return $data;
  }

  /**
   * Support for fwrite(), file_put_contents() etc.
   *
   * @param $data
   *   The string to be written.
   *
   * @return
   *   The number of bytes written (integer).
   *
   * @see http://php.net/manual/en/streamwrapper.stream-write.php
   */
  public function stream_write($data) {
    $this->write = TRUE; //write when flushed
    $this->stream_data .= $data;

    $byteswritten = strlen($data);

    $this->stream_pointer += $byteswritten;

    return $byteswritten;
  }

  /**
   * Support for feof().
   *
   * @return
   *   TRUE if end-of-file has been reached.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-eof.php
   */
  public function stream_eof() {
    //if the data is empty, get the data from the object
    //this stores all the data in the $stream_data property, only request once
    if (!$this->stream_data) {
      //if the object is empty, get the object from rackspace
      if (!$this->object) {
        $this->getContainer();
        $this->object = $this->container->get_object($target);
      }
      $this->stream_data = $this->object->read();
    }
    return $this->stream_pointer >= strlen($this->stream_data);
  }

  /**
   * Support for fseek().
   *
   * @param $offset
   *   The byte offset to got to.
   * @param $whence
   *   SEEK_SET, SEEK_CUR, or SEEK_END.
   *
   * @return
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-seek.php
   */
  public function stream_seek($offset, $whence) {
    switch ($whence) {
      case SEEK_SET:
        if (strlen($this->stream_data) >= $offset && $offset >= 0) {
          $this->stream_pointer = $offset;
          return TRUE;
        }
        return FALSE;
      case SEEK_CUR:
        if ($offset >= 0) {
          $this->stream_pointer += $offset;
          return TRUE;
        }
        return FALSE;
      case SEEK_END:
        if (strlen($this->stream_data) + $offset >= 0) {
          $this->stream_pointer = strlen($this->stream_data) + $offset;
          return TRUE;
        }
        return FALSE;
      default:
        return FALSE;
    }
    return FALSE;
  }

  /**
   * Support for fflush().
   *
   * @return
   *   TRUE if data was successfully stored (or there was no data to store).
   *
   * @see http://php.net/manual/en/streamwrapper.stream-flush.php
   */
  public function stream_flush() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "flushing file, writing to Rackspace (stream_flush) %file", array('%file' => $this->uri), WATCHDOG_DEBUG);
    }

    $this->getContainer();

    if (!$this->object) {
      $target = file_uri_target($this->uri);
      // Create it if it doesnt exist.
      $this->object = $this->container->create_object($target);
    }

    if (!$this->object->content_type) {
      // Rackspace will error if there is no content_type.
      $this->object->content_type = $this->getMimeType($this->uri);
    }
    if ($this->write) {
      // Allow other modules to add headers.
      module_invoke_all('cloud_files_build_object', $this->object, $this);
      // TODO: We should probalby return the result of this.
      $this->object->write($this->stream_data);
    }
    $this->stream_data = NULL;
    $this->stream_pointer = 0;
    return TRUE;
  }

  /**
   * Support for ftell().
   *
   * @return
   *   The current offset in bytes from the beginning of file.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-tell.php
   */
  public function stream_tell() {
    return $this->stream_pointer;
  }

  /**
   * Support for fstat().
   *
   * @return
   *   An array with file status, or FALSE in case of an error - see fstat()
   *   for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-stat.php
   */
  public function stream_stat() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing stream_stat", NULL, WATCHDOG_DEBUG);
    }
    return array(
      'size' => strlen($this->stream_data),
    );
  }

  /**
   * Support for fclose().
   *
   * @return
   *   TRUE if stream was successfully closed.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-close.php
   */
  public function stream_close() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing stream_close", NULL, WATCHDOG_DEBUG);
    }

    unset($this->object);
    $this->stream_data = NULL;
    $this->stream_pointer = 0;
    unset($this->stream_data); //unassign the reference.
    return TRUE;
  }

  /**
   * Support for unlink().
   *
   * @param $uri
   *   A string containing the uri to the resource to delete.
   *
   * @return
   *   TRUE if resource was successfully deleted.
   *
   * @see http://php.net/manual/en/streamwrapper.unlink.php
   */
  public function unlink($uri) {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing unlink", NULL, WATCHDOG_DEBUG);
    }

    $this->getContainer();
    return $this->container->delete_object($this->getLocalPath($uri));
  }

  /**
   * Support for rename().
   *
   * @param $from_uri,
   *   The uri to the file to rename.
   * @param $to_uri
   *   The new uri for file.
   *
   * @return
   *   TRUE if file was successfully renamed.
   *
   * @see http://php.net/manual/en/streamwrapper.rename.php
   */
  public function rename($from_uri, $to_uri) {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing rename (%from, %to)", array("%from" => $from_uri, "%to" => $to_uri), WATCHDOG_DEBUG);
    }

    //TODO rename
    return FALSE;
  }

  /**
   * Support for stat().
   * This important function goes back to the Unix way of doing things.
   * In this example almost the entire stat array is irrelevant, but the
   * mode is very important. It tells PHP whether we have a file or a
   * directory and what the permissions are. All that is packed up in a
   * bitmask. This is not normal PHP fodder.
   *
   * @param $uri
   *   A string containing the URI to get information about.
   * @param $flags
   *   A bit mask of STREAM_URL_STAT_LINK and STREAM_URL_STAT_QUIET.
   *
   * @return
   *   An array with file status, or FALSE in case of an error - see fstat()
   *   for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.url-stat.php
   */
  public function url_stat($uri, $flags) {
    if ($uri == NULL) {
      $uri = $this->uri;
    }

    $cache = &drupal_static(__METHOD__, array());
    if (isset($cache[$uri])) {
      //return $cache[$uri];
    }


    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "Checking file status (url_stat) %file", array('%file' => $uri), WATCHDOG_DEBUG);
    }

    $return = FALSE; // Default to fail.
    $mode = 0;

    $this->uri = $uri;
    $this->getContainer();
    $target = $this->getLocalPath($uri);


    //check if the file exists
    $objexists = TRUE;
    try{
      if (!empty($target)) {
        $this->object = $this->container->get_object($target);
      }
    }
    catch (NoSuchObjectException $e) {
      $objexists = FALSE;
    }

    if ($objexists) {
      //if the target is empty, its the root of the container (counts as a directory)
      if (empty($target) || $this->object->content_type == "application/directory") {
        $mode = 0040000; // S_IFDIR means it's a directory.
      }
      else {
        $mode = 0100000; // S_IFREG, means it's a file.
      }
      if ($mode) {
        $size = 0;
        if ($mode == 0100000) {
          $size = $this->object->content_length;
        }

        $mode |= 0777;
        $return = array(
          'dev' => 0,
          'ino' => 0,
          'mode' => $mode,
          'nlink' => 0,
          'uid' => 0,
          'gid' => 0,
          'rdev' => 0,
          'size' => $size,
          'atime' => 0,
          'mtime' => 0,
          'ctime' => 0,
          'blksize' => 0,
          'blocks' => 0,
        );
      }
    }

    $cache[$uri] = $return;
    return $return;
  }

  /**
   * Gets the name of the directory from a given path.
   *
   * @param $uri
   *   A URI.
   *
   * @return
   *   A string containing the directory name.
   *
   * @see drupal_dirname()
   */
  public function dirname($uri = NULL) {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing dirname (%uri)", array("%uri" => $uri), WATCHDOG_DEBUG);
    }

    list($scheme, $target) = explode('://', $uri, 2);
    $target  = $this->getTarget($uri);
    $dirname = dirname($target);

    if ($dirname == '.') {
      $dirname = '';
    }

    return $scheme . '://' . $dirname;
  }

  /**
   * Support for mkdir().
   *
   * @param $uri
   *   A string containing the URI to the directory to create.
   * @param $mode
   *   Permission flags - see mkdir().
   * @param $options
   *   A bit mask of STREAM_REPORT_ERRORS and STREAM_MKDIR_RECURSIVE.
   *
   * @return
   *   TRUE if directory was successfully created.
   *
   * @see http://php.net/manual/en/streamwrapper.mkdir.php
   */
  public function mkdir($uri, $mode, $options) {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing mkdir (%uri)", array("%uri" => $uri), WATCHDOG_DEBUG);
    }

    $this->uri = $uri;
    $this->getContainer();
    $target = file_uri_target($this->uri);

    //only make directories if there are directories to make
    if ($target) {
      //TODO check if it already exists?
      //TODO check options for recursive?
      $recursive = (bool) ($options & STREAM_MKDIR_RECURSIVE);

      //create_paths will create virtual folder files in Rackspace Cloud Files
      //this expects the a full path to a file
      //so add a file name to create all directories (wont create MKDIR.txt)
      $this->container->create_paths($target . '/MKDIR.txt');
    }

    return TRUE;
  }

  /**
   * Support for rmdir().
   *
   * @param $uri
   *   A string containing the URI to the directory to delete.
   * @param $options
   *   A bit mask of STREAM_REPORT_ERRORS.
   *
   * @return
   *   TRUE if directory was successfully removed.
   *
   * @see http://php.net/manual/en/streamwrapper.rmdir.php
   */
  public function rmdir($uri, $options) {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing rmdir (%uri)", array("%uri" => $uri), WATCHDOG_DEBUG);
    }

    // TODO: delete directory and all files
    return FALSE;
  }

  /**
   * Support for opendir().
   *
   * @param $uri
   *   A string containing the URI to the directory to open.
   * @param $options
   *   Unknown (parameter is not documented in PHP Manual).
   *
   * @return
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-opendir.php
   */
  public function dir_opendir($uri, $options) {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing dir_opendir (%uri)", array("%uri" => $uri), WATCHDOG_DEBUG);
    }

    /* TODO get list of items in directory */
    return FALSE;
  }

  /**
   * Support for readdir().
   *
   * @return
   *   The next filename, or FALSE if there are no more files in the directory.
   */
  public function dir_readdir() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing dir_readdir", NULL, WATCHDOG_DEBUG);
    }

    if ($this->directory_pointer < count($this->directory_list)) {
      $next = $this->directory_list[$this->directory_pointer];
      $this->directory_pointer++;
      return $next;
    }
    return FALSE;
  }

  /**
   * Support for rewinddir().
   *
   * @return
   *   TRUE on success.
   */
  public function dir_rewinddir() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing dir_rewinddir", NULL, WATCHDOG_DEBUG);
    }

      //TODO: what is this?
    $this->directory_pointer = 0;
  }

  /**
   * Support for closedir().
   *
   * @return
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-closedir.php
   */
  public function dir_closedir() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing dir_closedir", NULL, WATCHDOG_DEBUG);
    }

    $this->directory_pointer = 0;
    unset($this->directory_list);
    return TRUE;
  }

  /**
   * Implements abstract public function getDirectoryPath()
   */
  public function getDirectoryPath() {
    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "executing getDirectoryPath", NULL, WATCHDOG_DEBUG);
    }

    return "http://" . variable_get('rackspace_cloud_cdn_domain');
  }

  /**
   *
   */
  public function getContainer($uri = NULL) {
    $this->getCloudFilesContainer($uri);
    $container = $this->cloud_files_container;

    $cache = &drupal_static(__METHOD__, array());
    if (!empty($cache[$container->name])) {
      $this->container = $cache[$container->name];
      return;
    }
    libraries_load('rackspace');
    $auth = $this->getAuthentication();
    $conn = new CF_Connection($auth);

    if (variable_get('rackspace_cloud_debug')) {
      watchdog("cloud_files", "requesting container from Rackspace (%container)", array('%container' => variable_get('rackspace_cloud_container')), WATCHDOG_DEBUG);
    }

    $cache[$container->name] = $conn->get_container($this->cloud_files_container->name);
    $this->container = $cache[$container->name];
  }

  public function getAuthentication() {
    return cloud_files_get_authentication();
  }

  /**
   * Get the Cloud files container entity.
   *
   * The container may exist in the URI, othewise, get the default
   * container.
   *
   * @param $uri
   *   Optional; The URI to get from cache. This can be used for example
   *   to get the container of the origianl file for image-styles.
   *
   * @return
   *   A CloudFilesContainer entity.
   */
  private function getCloudFilesContainer($uri = NULL) {
    $name = cloud_files_container_cache($uri ? $uri : $this->uri);
    $this->cloud_files_container = cloud_files_get_container($name);
  }
}
