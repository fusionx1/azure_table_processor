<?php

/**
 * @file
 * Definition of FeedsDataProcessor.
 */

/**
 * Creates simple table records from feed items. Uses Data module.
 */
 
class AzureTableProcessor extends FeedsProcessor {

  // We aren't creating or updating entities, just table records.
  // So these are empty.
  public function entityType() {}
  protected function newEntity(FeedsSource $source) {}
  protected function entitySave($entity) {}
  protected function entityDeleteMultiple($entity_ids) {}

  /**
   * Implementation of FeedsProcessor::process().
   */
  public function process(FeedsSource $source, FeedsParserResult $parser_result) {
    // Count number of created and updated nodes.
    $inserted  = $updated = 0;
    $expiry_time = $this->expiryTime();
    $this->feed_nid = $source->feed_nid;

    // TODO: NEED to get Azure Table Name based off feeds_nid
    $table = $this->table($source);

    while ($data = $parser_result->shiftItem()) {
        AzureTableProcessor::dkan_entity_save($data, $this->tableName($source));
    }
  }
  /**
   * Count items imported by this source.
   */
  public function itemCount(FeedsSource $source) {
    // TODO: Determine the count in the Azure Table.
    $table_name = $this->tableName($source);
    if (db_table_exists($table_name)) {
      return db_query("SELECT count(*) FROM {" . $table_name . "}")->fetchField();
    }
    else {
      return NULL;
    }
  }

  /**
   * Implementation of FeedsProcessor::clear().
   *
   * Delete all data records for feed_entity_id in this table.
   */
  public function clear(FeedsSource $source) {
    // TODO: Determine the count in the Azure Table.
    $table_name = $this->tableName($source);
    $result = db_truncate($table_name)->execute();
    if ($result) {
      drupal_set_message('All items have been deleted.');
    }
    else {
      drupal_set_message('There were no items to delete.');
    }
  }

  /**
   * Implement expire().
   */
  public function expire($time = NULL) {
    if ($time === NULL) {
      $time = $this->expiryTime();
    }
    if ($time == FEEDS_EXPIRE_NEVER) {
      return FEEDS_BATCH_COMPLETE;
    }
    $clause = array(
      'timestamp' => array(
        '<',
        FEEDS_REQUEST_TIME - $time,
      ),
    );
    // TODO: To do, use Azures SDK delete.
    $num = $this->handler($source)->delete($clause);
    drupal_set_message(format_plural($num, 'Expired @number record from @table.', 'Expired @number records from @table.', array('@number' => $num, '@table' => $this->tableName())));
    return FEEDS_BATCH_COMPLETE;
  }

  /**
   * Return expiry time.
   */
  public function expiryTime() {
    return $this->config['expire'];
  }

  /**
   * Set target element, bring element in a FeedsDataHandler format.
   */
  public function setTargetElement(FeedsSource $source, $target_item, $target_element, $value) {
    if (empty($value)) {
      return;
    }
    if (strpos($target_element, '.')) {
      /**
      Add field in FeedsDataHandler format.

      This is the tricky part, FeedsDataHandler expects an *array* of records
      at #[joined_table_name]. We need to iterate over the $value that has
      been mapped to this element and create a record array from each of
      them.
      */
      list($table, $field) = explode('.', $target_element);

      $values = array();
      $value = is_array($value) ? $value : array($value);
      foreach ($value as $v) {
        // Create a record array.
        $values[] = array(
          $field => $v,
        );
      }
      if (is_array($target_item["#$table"])) {
        $target_item["#$table"] = array_merge($target_item["#$table"], $values);
      }
      else {
        $target_item["#$table"] = $values;
      }
    }
    else {
      if (is_array($target_item[$target_element]) && is_array($value)) {
        $target_item[$target_element] = array_merge($target_item[$target_element], $value);
      }
      else {
        $target_item[$target_element] = $value;
      }
    }
  }

  /**
   * Iterate through unique targets and try to load existing records.
   * Return id for the first match.
   */
  protected function existingItemId(FeedsParserResult $parser_result, FeedsSource $source) {
    foreach ($this->uniqueTargets($source, $parser_result) as $target => $value) {
      if ($records = $this->handler($source)->load(array('feed_entity_id' => $source->feed_entity_id, $target => $value))) {
        return $records[0]['id'];
      }
    }
    return FALSE;
  }

  /**
   * Override parent::configDefaults().
   */
  public function configDefaults() {
    return array(
      'update_existing' => FEEDS_SKIP_EXISTING,
      'expire' => FEEDS_EXPIRE_NEVER, // Don't expire items by default.
      'mappings' => array(),
      'delete_with_source' => FALSE,
    );
  }

  /**
   * Override parent::configForm().
   */
  public function configForm(&$form_state) {
    $period = drupal_map_assoc(array(FEEDS_EXPIRE_NEVER, 3600, 10800, 21600, 43200, 86400, 259200, 604800, 604800 * 4, 604800 * 12, 604800 * 24, 31536000), 'feeds_format_expire');
    $form['expire'] = array(
      '#type' => 'select',
      '#title' => t('Expire items'),
      '#options' => $period,
      '#description' => t('Select after how much time data records should be deleted. The timestamp target value will be used for determining the item\'s age, see Mapping settings.'),
      '#default_value' => $this->config['expire'],
    );
    $form['update_existing'] = array(
      '#type' => 'checkbox',
      '#title' => t('Replace existing records'),
      '#description' => t('If an existing record is found for an imported record, replace it. Existing records will be determined using mappings that are a "unique target".'),
      '#default_value' => $this->config['update_existing'],
    );
    return $form;
  }

  /**
   * Reschedule if expiry time changes.
   */
  public function configFormSubmit(&$values) {
    if ($this->config['expire'] != $values['expire']) {
      feeds_reschedule($this->id);
    }
    parent::configFormSubmit($values);
  }

  /**
   * Return the data table name for this feed.
   */
  protected function tableName(FeedsSource $source) {
    // TODO: Determine the Azure Table name.
    return 'feedsdatastore'. $source->feed_nid;
  }

  /**
   * Return the data table for this feed.
   *
   * @throws Exception $e
   *   Throws this exception if a table cannot be found and cannot be created.
   *
   * @todo Make *Data module* throw exception when table can't be found or
   *   can't be created.
   */
  protected function table(FeedsSource $source) {
  $tableRestProxy = AzureTableProcessor::dkan_table_store_connection();
    // TODO: Create and / or retrieve info from Azure Table.
    try{  
       $tableRestProxy->getTable($this->tableName($source));
    }catch(WindowsAzure\Common\ServiceException $e){
       $code = $e->getCode();
       $error_message = $e->getMessage();
       if($code == 404){ //table doestn exist
	     $table = AzureTableProcessor::dkan_create_table($this->tableName($source));
	     return $this->tableName($source);
	   }elseif($code == 409){ //table exist
	     return $this->tableName($source);
	   }
    }  
  }
  

function dkan_entity_save($data, $feed_nid){
  $tableRestProxy = AzureTableProcessor::dkan_table_store_connection();

  $nowDT = new DateTime('now', new DateTimeZone('UTC'));
  $partitionKey = $nowDT->format("mdY");
  $rowKey = uniqid();

  $entity = new WindowsAzure\Table\Models\Entity();
  $entity->setPartitionKey($feed_nid);
  $entity->setRowKey($rowKey);

  foreach($data as $key => $val){
    $entity->addProperty($key, WindowsAzure\Table\Models\EdmType::STRING, $data[$key]);
  }
  try{
      $tableRestProxy->insertOrReplaceEntity($feed_nid, $entity);
  } catch(WindowsAzure\Common\ServiceException $e){
    $code = $e->getCode();
    $error_message = $e->getMessage();
    //echo $code.": ".$error_message."<br />";
  } 
 
} 

 
function dkan_create_table($source){
$tableRestProxy = AzureTableProcessor::dkan_table_store_connection();

 try {
      // Create table.
      $tableRestProxy->createTable($source);
	  return true;
  }
  catch(WindowsAzure\Common\ServiceException $e){
    $code = $e->getCode();
    $error_message = $e->getMessage();
    // Handle exception based on error codes and messages.
    // Error codes and messages can be found here: 
    // http://msdn.microsoft.com/en-us/library/windowsazure/dd179438.aspx
  }

}

function dkan_table_store_connection(){
  require_once '/var/www/drupalcamp/sites/all/libraries/vendor/autoload.php';

  $connectionString=  'DefaultEndpointsProtocol=https;AccountName=nuamsdevelopers;AccountKey=go7XHIx7fanEOYX6YeDH4AKw8jCyDMAwzEHTaSHzVSCHbPAmBHfiuBQ7UPeSdLDvUR+VaMR1zar7UYMa13DLtQ==';
  // Create table REST proxy.
  $connection = WindowsAzure\Common\ServicesBuilder::getInstance()->createTableService($connectionString);
  return $connection;
}


  /**
   * Return a data handler for this table.
   *
   * Avoids a call to table() to not unnecessarily instantiate DataTable.
   */
  protected function handler(FeedsSource $source) {
    return DataHandler::instance($this->tableName($source), 'id');
  }


 /**
   * Return available mapping targets.
   */
  public function getMappingTargets() {
    $type = node_type_get_type($this->bundle());

    $targets = parent::getMappingTargets();
  
      $targets['title'] = array(
        'name' => t('Title'),
        'description' => t('The title of the node.'),
        'optional_unique' => TRUE,
      );
    
    $targets['nid'] = array(
      'name' => t('Node ID'),
      'description' => t('The nid of the node. NOTE: use this feature with care, node ids are usually assigned by Drupal.'),
      'optional_unique' => TRUE,
    );
    $targets['uid'] = array(
      'name' => t('User ID'),
      'description' => t('The Drupal user ID of the node author.'),
    );
    $targets['user_name'] = array(
      'name' => t('Username'),
      'description' => t('The Drupal username of the node author.'),
    );
    $targets['user_mail'] = array(
      'name' => t('User email'),
      'description' => t('The email address of the node author.'),
    );
    $targets['status'] = array(
      'name' => t('Published status'),
      'description' => t('Whether a node is published or not. 1 stands for published, 0 for not published.'),
    );
    $targets['created'] = array(
      'name' => t('Published date'),
      'description' => t('The UNIX time when a node has been published.'),
    );
    $targets['promote'] = array(
      'name' => t('Promoted to front page'),
      'description' => t('Boolean value, whether or not node is promoted to front page. (1 = promoted, 0 = not promoted)'),
    );
    $targets['sticky'] = array(
      'name' => t('Sticky'),
      'description' => t('Boolean value, whether or not node is sticky at top of lists. (1 = sticky, 0 = not sticky)'),
    );

    // Include language field if Locale module is enabled.
    if (module_exists('locale')) {
      $targets['language'] = array(
        'name' => t('Language'),
        'description' => t('The two-character language code of the node.'),
      );
    }

    // Include comment field if Comment module is enabled.
    if (module_exists('comment')) {
      $targets['comment'] = array(
        'name' => t('Comments'),
        'description' => t('Whether comments are allowed on this node: 0 = no, 1 = read only, 2 = read/write.'),
      );
    }

    // If the target content type is a Feed node, expose its source field.
    if ($id = feeds_get_importer_id($this->bundle())) {
      $name = feeds_importer($id)->config['name'];
      $targets['feeds_source'] = array(
        'name' => t('Feed source'),
        'description' => t('The content type created by this processor is a Feed Node, it represents a source itself. Depending on the fetcher selected on the importer "@importer", this field is expected to be for example a URL or a path to a file.', array('@importer' => $name)),
        'optional_unique' => TRUE,
      );
    }

    // Let other modules expose mapping targets.
    self::loadMappers();
    $entity_type = $this->entityType();
    $bundle = $this->bundle();
    drupal_alter('feeds_processor_targets', $targets, $entity_type, $bundle);

    return $targets;
  }



  /**
   * At some point we might want to determine best field type in
   * the database by the type of content in the first row of each.
   * This is difficult because PHP thinks everything is a string.
   */
  function dataSchema($first_row) {
    foreach ($first_row as $field => $data) {
      // Default to bigtext for now.
      $data_schema[$field] = 'bigtext';
    }
    return $data_schema;
  }

  /**
   * Creates full schema from baseSchema and an array.
   *
   *  @param array data
   *  Arrway with keys for each desired row.
   */
  function fullSchema($data) {
    $base = $this->baseSchema();
    $data_schema = data_build_schema($data);
    $base['fields'] = array_merge($data_schema['fields'], $base['fields']);
    return $base;
  }

  /**
   * Every Feeds data table must have these elements.
   */
  protected function baseSchema() {
    return array(
      'fields' => array(
        'id' => array(
          'type' => 'serial',
          'size' => 'normal',
          'unsigned' => TRUE,
          'not null' => TRUE,
        ),
        'timestamp' => array(
          'description' => 'The Unix timestamp for the data.',
          'type' => 'int',
          'unsigned' => TRUE,
          'not null' => FALSE,
        ),
      ),
      'indexes' => array(
        'id' => array('id'),
        'timestamp' => array('timestamp'),
       ),
       'primary key' => array(
         '0' => 'id',
       ),
    );
  }

}
