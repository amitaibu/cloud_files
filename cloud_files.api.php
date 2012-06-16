<?php

/**
 * @file
 * Hooks provided by the Cloud files module.
 *
 */

/**
 * @addtogroup hooks
 * @{
 */

/**
 * Allow modules to add headers to the object before it is written
 *
 * @param $object
 *   The Cloud fiels object.
 * @param $context
 *   The stream wrapper object, used for reference.
 */
function hook_cloud_files_build_object($object, $context) {
  // Add CORS to file.
  $this->object->headers = array(
    'Access-Control-Allow-Origin' => '*',
  );
}

/**
 * @} End of "addtogroup hooks".
 */