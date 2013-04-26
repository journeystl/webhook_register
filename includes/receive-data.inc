<?php

/**
 * Receive $_POST data from a webhook.
 */
function webhook_register_receive_data() {
  // Require $_POST data.
  if (!isset($_POST['data'])) {
    return MENU_NOT_FOUND;
  }

  // Require properly signed $_POST data.
  if (!isset($_POST['signed']) || !_webhook_register_verify_data($_POST)) {
    return MENU_ACCESS_DENIED;
  }

  // Load jsonPath and convert the json data into a usable array.
  require(__DIR__ . '/../jsonpath-0.8.1.php');
  $raw = $_POST['data'];
  $array = json_decode(utf8_encode($raw), TRUE);
  if (!is_array($array)) {
    $raw = preg_replace('/}\s*{/', '},{', $raw);
    $raw = '[' . $raw . ']';
    $array = json_decode(utf8_encode($raw), TRUE);
  }

  // Start the import.
  $importer = db_query("SELECT * FROM webhook_register_importer WHERE remote_type = :node_type", array(':node_type' => $array['type']))->fetch();
  if ($importer) {
    $importer->settings = unserialize($importer->settings);

    // Check if we already have this node.
    $nid = db_query("SELECT nid FROM webhook_register_hash WHERE guid = :guid", array(':guid' => $array['guid']))->fetchColumn();
    if ($nid) {
      $node = node_load($nid);
    } else {
      $node = new stdClass();
      $node->type = $importer->node_type;
      node_object_prepare($node);
      $node->language = LANGUAGE_NONE;
      $node->uid = 0;
      $node->status = 0;
      node_save($node);
    }

    // Properties
    foreach ($importer->settings['properties'] as $property_name => $property_path) {
      $json_data = jsonPath($array, $property_path);
      $node->{$property_name} = reset($json_data);
    }

    // Fields
    foreach ($importer->settings['fields'] as $field_name => $field_json_path) {
      $json_data = jsonPath($array, $field_json_path);
      $field_info_field = field_info_field($field_name);
      if ($json_data) {
        foreach ($json_data as $key => $val) {

          switch ($field_info_field['type']) {

            // This media item should reference an existing node.
            // The value here should equate to the local GUID of the target.
            case 'entityreference':
              $reference_nid = db_query("SELECT nid FROM webhook_register_hash WHERE guid = :guid", array(':guid' => $val))->fetchColumn();
              $node->{$field_name}[LANGUAGE_NONE][$key]['target_id'] = $reference_nid;
              break;

            case 'file':
            case 'image':
              $save_file_result = webhook_register_receive_data_saveFile($val);
              if (isset($save_file_result['error'])) {
                // TODO delete the node we may have created above?
                print $save_file_result['error'];
                exit();
              }
              $node->{$field_name}[LANGUAGE_NONE][$key] = (array) $save_file_result['file'];
              break;

            default:
              $node->{$field_name}[LANGUAGE_NONE][$key]['value'] = $val;
              break;

          }
        }
      } else {
        // TODO error logging
      }
    }

    // Field collections.
    // We must either load any existing field collections for this node or create them, as well as
    // delete any field_collections beyond the current count of data.
    //
    // Step 1 – gather all of the data for this field collection.
    foreach ($importer->settings['field_collections'] as $field_collection_name => $field_collection_fields) {
      $field_collections[$field_collection_name] = array();
      foreach ($field_collection_fields as $field_collection_field_name => $field_collection_field_json_path) {
        $json_data = jsonPath($array, $field_collection_field_json_path);
        $field_collections[$field_collection_name][$field_collection_field_name] = $json_data;
      }
    }

    // Step 2 - load/create field_collection_items for this node and attach the data.
    foreach ($field_collections as $field_collection_name => $field_collection_fields_data) {
      $num_new_collections = count(reset($field_collection_fields_data));
      for ($i = 0; $i < $num_new_collections; $i++) {
        // Load/create field_collection_item.
        if (isset($node->{$field_collection_name}[LANGUAGE_NONE], $node->{$field_collection_name}[LANGUAGE_NONE][$i])) {
          $field_collection_item = entity_load('field_collection_item', array($node->{$field_collection_name}[LANGUAGE_NONE][$i]['value']));
          $field_collection_item = reset($field_collection_item);
        } else {
          $field_collection_item = entity_create('field_collection_item', array('field_name' => $field_collection_name));
          $field_collection_item->setHostEntity('node', $node);
        }

        foreach ($field_collection_fields_data as $field_collection_field_name => $field_collection_field_values) {
          $field_info_field = field_info_field($field_collection_field_name);
          if (isset($field_collection_field_values[$i])) {
            switch ($field_info_field['type']) {

              case 'file':
              case 'image':
                $save_file_result = webhook_register_receive_data_saveFile($field_collection_field_values[$i]);
                if (isset($save_file_result['error'])) {
                  // TODO delete the node we may have created above?
                  print $save_file_result['error'];
                  exit();
                }
                $field_collection_item->{$field_collection_field_name}[LANGUAGE_NONE][0] = (array) $save_file_result['file'];
                break;

              default:
                $field_collection_item->{$field_collection_field_name}[LANGUAGE_NONE][0]['value'] = $field_collection_field_values[$i];
                break;

            }
          }
        }
        $field_collection_item->save();
      }

      // Delete any field collections higher than the number we created/updated above.
      if (isset($node->{$field_collection_name}[LANGUAGE_NONE]) && ( count($node->{$field_collection_name}[LANGUAGE_NONE]) > $num_new_collections) ) {
        for ($i = $num_new_collections; $i <= count($node->{$field_collection_name}[LANGUAGE_NONE]); $i++) {
          print 'i:('.$i.').';
          // Delete the field collection.
          entity_delete('field_collection_item', $node->{$field_collection_name}[LANGUAGE_NONE][$i]['value']);
          // Remove its reference from the node.
          unset($node->{$field_collection_name}[LANGUAGE_NONE][$i]);
        }
      }
    }

    node_save($node);

    db_merge('webhook_register_hash')
      ->key(array('guid' => $array['guid']))
      ->fields(array(
        'nid' => $node->nid,
        'importer' => $importer->id,
        'hash' => 'hash',
        'timestamp' => time(),
      ))
      ->execute();
  }

  print 'success';
  exit();
}

/**
 * Save a file from a given URL and return a file object.
 * TODO this should support a directory param.
 */
function webhook_register_receive_data_saveFile($url) {
  $return = array();

  // Download the file.
  if (!$file_data = file_get_contents($url)) {
    $return['error'] = "Could not download {$url}.";
    return $return;
  }

  $path_parts = pathinfo($url);

  // Save the file.
  $file = file_save_data($file_data, "public://{$path_parts['filename']}.{$path_parts['extension']}", FILE_EXISTS_REPLACE);
  file_usage_add($file, 'webhook_register', 'node', $file->fid);

  $return['file'] = $file;
  return $return;
}

/**
 * Verify that $_POST data is signed using the proper API key.
 * @param $_POST data containing both a 'data' and a 'signed' value.
 */
function _webhook_register_verify_data($post) {
  $key = variable_get('webhook_register_api_key');
  $signed = hash_hmac('sha256', $post['data'], $key);
  return ($signed === $post['signed']);
}
