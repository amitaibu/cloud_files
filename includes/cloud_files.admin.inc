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

  $form['rackspace_cloud_auth_url'] = array(
    '#type' => 'radios',
    '#title' => t('Authentication URL'),
    '#description' => t('Select "United States" if your account was created via rackspacecloud.com. Select "United Kingdom" if your account was created via rackspace.co.uk.'),
    '#options' => array(
      US_AUTHURL => t('United States'),
      UK_AUTHURL => t('United Kingdom'),
      'other'    => t('Other'),
    ),
    '#default_value' => $default_value,
    '#required' => TRUE,
  );

  $other_url = !in_array($default_value, array(US_AUTHURL, UK_AUTHURL)) ? $default_value : '';
  $form['rackspace_cloud_auth_url_other'] = array(
    '#type' => 'textfield',
    '#title' => t('Other authentication URL'),
    '#default_value' => $other_url,
    '#states' => array(
      'visible' => array(
        ':input[name="rackspace_cloud_auth_url"]' => array('value' => 'other'),
      ),
    ),
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
    '#type' => 'textfield',
    '#title' => t('API Key'),
    '#description' => t('Rackspace Cloud API Key.'),
    '#default_value' => $rackspace_cloud_api_key,
    '#required' => TRUE,
  );

  $form['rackspace_cloud_temp_url_key'] = array(
    '#type' => 'textfield',
    '#title' => t('Temp URL key'),
    '#description' => t('The secret key needed for creating <a href="http://docs.rackspace.com/files/api/v1/cf-devguide/content/Set_Account_Metadata-d1a4460.html">temporary URLs</a>.'),
    '#default_value' => variable_get('rackspace_cloud_temp_url_key'),
  );


  $form['rackspace_cloud_container'] = array(
    '#type' => 'textfield',
    '#title' => t('Default container'),
    '#description' => t('Default Rackspace Cloud Container name.'),
    '#default_value' => variable_get('rackspace_cloud_container'),
    '#required' => TRUE,
  );

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

  if ($form_state['values']['rackspace_cloud_auth_url'] == 'other') {
    // If using Other authentication URL, set its value from the text
    // field.
    form_set_value('rackspace_cloud_auth_url', $form_state['values']['rackspace_cloud_auth_url_other'], $form_state);
  }
  $auth_url = $form_state['values']['rackspace_cloud_auth_url'];

  $authenticated = FALSE;
  if (variable_get('rackspace_cloud_username') != $user || variable_get('rackspace_cloud_api_key') != $apikey || variable_get('rackspace_cloud_auth_url') != $auth_url) {
    try {
      $auth = new CF_Authentication($user, $apikey, NULL, $auth_url);
      if ($auth->authenticate()) {
        // Authentication is correct.
        $authenticated = TRUE;
        variable_set('rackspace_cloud_username', $user);
        variable_set('rackspace_cloud_api_key', $apikey);
        variable_set('rackspace_cloud_auth_url', $auth_url);
      }
    }
    catch (AuthenticationException $e) {
      form_set_error('rackspace_cloud_username][rackspace_cloud_api_key', t('Invalid Username or API Key.'));
    }
    catch (Exception $e) {
      form_set_error('rackspace_cloud_username][rackspace_cloud_api_key', t('Error authenticating. Please check the Username and/or API Key.'));
    }
  }
  else {
    $authenticated = TRUE;
  }

  if ($authenticated) {
    // Validate the default container.
  }
}
