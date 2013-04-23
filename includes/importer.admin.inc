<?php

/**
 * Manage an importer.
 */
function webhook_register_importer_admin_form($form, &$form_state, $importer_id = NULL) {
  // Load the importer.
  if ($importer_id) {
    $form['#importer'] = db_query("SELECT * FROM webhook_register_importer WHERE id = :importer_id", array(':importer_id' => $importer_id))->fetch();
    if (!$form['#importer']) {
      drupal_set_message('The importer was not found.', 'error');
    }
    $form_state['#node_type'] = $form['#importer']->node_type;
    $importer_settings = unserialize($form['#importer']->settings);
    drupal_set_title('Editing Importer: <em>' . check_plain($form['#importer']->name) . '</em>', PASS_THROUGH);
  }

  // Get the current field count.
  if (!isset($form_state['#num_fields'])) {
    if (isset($importer_settings)) {
      $form_state['#num_fields'] = count($importer_settings['fields']);
    } else {
      $form_state['#num_fields'] = 0;
    }
  }

  $form['#prefix'] = '<div id="webhook_register_importer">';
  $form['#suffix'] = '</div>';

  $form['name'] = array(
    '#type' => 'textfield',
    '#title' => 'Name',
    '#default_value' => (isset($form['#importer'])) ? $form['#importer']->name : NULL,
  );

  $form['node_type'] = array(
    '#title' => 'Node Type',
    '#type' => 'select',
    '#options' => array('_none' => '- Select -') + db_query("SELECT type, name FROM node_type")->fetchAllKeyed(),
    '#default_value' => (isset($form['#importer'])) ? $form['#importer']->node_type : NULL,
    '#ajax' => array(
      'callback' => 'webhook_register_importer_ajaxGetNodeType',
      'wrapper' => 'webhook_register_importer',
    ),
  );

  $form['json_path_desc']['#markup'] = '<p>Enter the JSON paths for the properties and fields below.</p>';

  // Node property assignments.
  $form['properties'] = array(
    '#type' => 'fieldset',
    '#title' => 'Properties',
    '#tree' => TRUE,
    '#collapsed' => TRUE,
    '#collapsible' => TRUE,
    'title' => array(
      '#title' => 'Title',
      '#type' => 'textfield',
      '#default_value' => $importer_settings['properties']['title'],
    ),
    'status' => array(
      '#title' => 'Published',
      '#type' => 'textfield',
      '#default_value' => $importer_settings['properties']['status'],
    ),
    'sticky' => array(
      '#title' => 'Sticky',
      '#type' => 'textfield',
      '#default_value' => $importer_settings['properties']['sticky'],
    ),
  );

  // Show fields and field collections for this node type / importer.
  if (isset($form_state['#node_type'])) {
    $form['fields'] = array(
      '#type' => 'fieldset',
      '#title' => 'Fields',
      '#tree' => TRUE,
      '#collapsed' => TRUE,
      '#collapsible' => TRUE,
    );

    $form['field_collections'] = array(
      '#type' => 'fieldset',
      '#title' => 'Field Collections',
      '#tree' => TRUE,
      '#collapsed' => TRUE,
      '#collapsible' => TRUE,
    );

    // Get all fields for this node type.
    $field_info_instances = field_info_instances('node', $form_state['#node_type']);
    foreach ($field_info_instances as $field_info_instance) {
      $field_info_field = field_info_field($field_info_instance['field_name']);

      // Handle different field types.
      switch ($field_info_field['type']) {

        case 'text':
        default:
          $form['fields'][$field_info_instance['field_name']] = array(
            '#title' => "{$field_info_instance['label']} ({$field_info_instance['field_name']})",
            '#type' => 'textfield',
            '#default_value' => $importer_settings['fields'][$field_info_instance['field_name']],
          );
          break;

        case 'field_collection':
          $form['field_collections'][$field_info_instance['field_name']] = array(
            '#type' => 'fieldset',
            '#title' => "{$field_info_instance['label']} ({$field_info_instance['field_name']})",
          );
          foreach (field_info_instances('field_collection_item', $field_info_instance['field_name']) as $field_collection_item_instance) {
            $form['field_collections'][$field_info_instance['field_name']][$field_collection_item_instance['field_name']] = array(
              '#type' => 'textfield',
              '#title' => "{$field_collection_item_instance['label']} ({$field_collection_item_instance['field_name']})",
              '#default_value' => $importer_settings['field_collections'][$field_info_instance['field_name']][$field_collection_item_instance['field_name']],
            );
          }
          break;

      }

    }
  }

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => 'Submit',
  );

  return $form;
}

function webhook_register_importer_admin_form_ajaxGetNodeType($form, $form_state) {
  $form_state['#importer']->node_type = $form_state['values']['node_type'];
  return drupal_rebuild_form('webhook_register_importer_admin_form', $form_state, $form);
}

function webhook_register_importer_admin_form_submit(&$form, &$form_state) {
  $settings = array();

  // Add node properties to settings.
  $settings['properties'] = $form_state['values']['properties'];

  // Add fields to settings.
  $settings['fields'] = $form_state['values']['fields'];

  // Add field collections to settings.
  $settings['field_collections'] = $form_state['values']['field_collections'];

  // Update the database.
  if (isset($form['#importer']->id)) {
    drupal_set_message('Importer updated.');
    db_update('webhook_register_importer')
      ->fields(array(
        'name' => $form_state['values']['name'],
        'node_type' => $form_state['values']['node_type'],
        'settings' => serialize($settings),
      ))
      ->condition('id', $form['#importer']->id)
      ->execute();
  } else {
    db_insert('webhook_register_importer')
      ->fields(array(
        'name' => $form_state['values']['name'],
        'node_type' => $form_state['values']['node_type'],
        'settings' => serialize($settings),
      ))
      ->execute();
    drupal_set_message('Importer created.');
  }
}
