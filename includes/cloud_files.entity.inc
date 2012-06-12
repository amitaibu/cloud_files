<?php
/**
 * @file
 * Entity API controller for Cloud file container.
 */

 /**
 * A class used for message types category.
 */
class CloudFilesContainer extends Entity {

  /**
   * Boolean indicating whether the container is published to CDN.
   *
   * @var boolean
   */
  public $published = FALSE;

  /**
   * The CDN domain URI for a published container.
   *
   * @var string
   */
  public $cdn_domain = '';

  /**
   * The secret key for the temporary URL.
   *
   * @var string
   */
  public $temp_url_key = '';

  /**
   * The expiration time of the temporary URL in seconds.
   *
   * @var integer
   */
  public $temp_url_expire = 0;


  public function __construct($values = array()) {
    parent::__construct($values, 'cloud_files_container');
  }
}