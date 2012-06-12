<?php

/**
 * @file
 * Cloud file admin settings.
 */

/**
 * Page callback; Admin settings.
 */
function cloud_files_admin_settings() {
  libraries_load('rackspace');

  $default_value = variable_get('rackspace_cloud_auth_url', US_AUTHURL);
  $options = array(
    US_AUTHURL => t('United States'),
    UK_AUTHURL => t('United Kingdom'),
  );

  if (in_array($default_value, array(US_AUTHURL, UK_AUTHURL))) {
    $key = 'other';
    $other_url = '';
  }
  else {
    $key = $default_value;
    $other_url = $default_value;
  }


  $options[$key] = t('Other');

  $form['rackspace_cloud_auth_url'] = array(
    '#type' => 'radios',
    '#title' => t('Authentication URL'),
    '#description' => t('Select "United States" if your account was created via rackspacecloud.com. Select "United Kingdom" if your account was created via rackspace.co.uk.'),
    '#required' => TRUE,
    '#options' => $options,
    '#default_value' => $default_value,
  );

  $form['rackspace_cloud_auth_url_other'] = array(
    '#type' => 'textfield',
    '#title' => t('Other authentication URL'),
    '#default_value' => $other_url,
  );

  $rackspace_cloud_username = variable_get('rackspace_cloud_username');
  $rackspace_cloud_api_key = variable_get('rackspace_cloud_api_key');
  $rackspace_cloud_container = variable_get('rackspace_cloud_container');
  $rackspace_cloud_cdn_domain = variable_get('rackspace_cloud_cdn_domain');

  $form['rackspace_cloud_username'] = array(
    '#type' => 'textfield',
    '#title' => t('Username'),
    '#description' => t('Rackspace Cloud username.'),
    '#default_value' => $rackspace_cloud_username,
    '#required' => TRUE,
  );

  $form['rackspace_cloud_api_key'] = array(
    '#type' => 'password',
    '#title' => t('API Key'),
    '#description' => t('Rackspace Cloud API Key.'),
    '#default_value' => $rackspace_cloud_api_key,
  );

  // Try to get the containers list
  if ($rackspace_cloud_username && $rackspace_cloud_api_key) {
    try {
      $auth = cloud_files_get_authentication();
      $conn = new CF_Connection($auth);
      $container_list = $conn->list_containers();

      $container_options = array();
      foreach ($container_list as $container) {
        $container_options[$container] = $container;
      }
      if ($container_list) {
        $form['rackspace_cloud_container'] = array(
          '#type' => 'select',
          '#title' => t('Container'),
          '#description' => t('Default Rackspace Cloud Container name.'),
          '#default_value' => $container_options,
          '#required' => TRUE,
          '#access' => $container_options,
        );
      }
    }
    catch (Exception $e) {
      drupal_set_message(t('Error getting container list.'), 'error');
    }
  }

  $form['rackspace_cloud_cdn_domain'] = array(
    '#type' => 'value',
    '#title' => t('Domain'),
    '#description' => t('The container domain. Enter a custom CNAME here. Leave this empty to get the default domain of the selected container.'),
    '#default_value' => $rackspace_cloud_cdn_domain,
  );
  //if there is a domain set, allow it to be updated
  //this is so it is first set to the container domain name
  //but can be updated to a custom CNAME
  if ($rackspace_cloud_cdn_domain) {
    $form['rackspace_cloud_cdn_domain']['#type'] = 'textfield';
    $form['rackspace_cloud_cdn_domain']['#required'] = TRUE;
  }

  $form['rackspace_cloud_debug'] = array(
    '#type' => 'checkbox',
    '#title' => t('Debug'),
    '#description' => t('Check the box to enable logging.'),
    '#default_value' => variable_get('rackspace_cloud_debug'),
  );

  return system_settings_form($form);
}

/**
 * Validate handler; Admin settings.
 */
function cloud_files_admin_settings_validate($form, &$form_state) {
  libraries_load('rackspace');

  $user = $form_state['values']['rackspace_cloud_username'];
  $apikey = $form_state['values']['rackspace_cloud_api_key'] ? $form_state['values']['rackspace_cloud_api_key'] : variable_get('rackspace_cloud_api_key');
  $container = $form_state['values']['rackspace_cloud_container'];
  $rackspace_cloud_cdn_domain = $form_state['values']['rackspace_cloud_cdn_domain'];

  if ($form_state['values']['rackspace_cloud_auth_url'] == 'other') {
    // If using Other authentication URL, set its value from the text
    // field.
    form_set_value('rackspace_cloud_auth_url', $form_state['values']['rackspace_cloud_auth_url_other'], $form_state);
  }
  $auth_url = $form_state['values']['rackspace_cloud_auth_url'];

  try {
    $auth = new CF_Authentication($user, $apikey, NULL, $auth_url);
    if ($auth->authenticate()) {
      // Authentication is correct.
      variable_set('rackspace_cloud_username',$user);
      variable_set('rackspace_cloud_api_key',$apikey);
      variable_set('rackspace_cloud_auth_url',$auth_url);
    }
  }
  catch (AuthenticationException $e) {
    form_set_error('rackspace_cloud_username][rackspace_cloud_api_key', t('Invalid Username or API Key.'));
  }
  catch (Exception $e) {
    form_set_error('rackspace_cloud_username][rackspace_cloud_api_key', t('Error authenticating. Please check the Username and/or API Key.'));
  }

  //if there is not a domain set, get the container domain
  if (!$rackspace_cloud_cdn_domain) {
    try {
      $conn = new CF_Connection($auth);
      $container = $conn->get_container($container);
      $form_state['values']['rackspace_cloud_cdn_domain'] = str_replace(array('http://', 'https://'), '', $container->cdn_uri);
    }
    catch (Exception $e) {
      form_set_error('rackspace_cloud_container', t('Container does not exist.'));
    }
  }
}