<?php

function webhook_register_importer_history($importer_id) {
  $importer = db_query("SELECT * FROM webhook_register_importer WHERE id = :importer_id", array(':importer_id' => $importer_id))->fetch();
  drupal_set_title("History for Importer <em>{$importer->name}</em>", PASS_THROUGH);

  // Get all nodes for this importer.
  $nodes = db_query("SELECT n.nid, n.type, n.title, w.timestamp FROM node n LEFT JOIN webhook_register_hash w ON w.nid = n.nid WHERE w.importer = :importer_id", array(':importer_id' => $importer_id))->fetchAll();

  $page = array(
    '#theme' => 'table',
    '#header' => array('NID', 'Title', 'Type', 'Date Created / Last Updated'),
    '#rows' => array(),
  );

  foreach ($nodes as $node) {
    $page['#rows'][] = array(
      $node->nid,
      l($node->title, "node/{$node->nid}"),
      $node->type,
      date("Y-m-d h:ia", $node->timestamp),
    );
  }

  return $page;
}
