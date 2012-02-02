<?php

/**
 * @file
 * SveaWebPay administration menu items.
 */

/**
 * Define Constants.
 */
define('UC_SVEAWEBPAY_SUCCESS', 0);

/**
 * Validate MAC (hash) from SveaWebPay.
 *
 * @param $data_to_validate
 *   A string containing the data to validate.
 *
 * @param $mac_to_validate
 *   A string containing the MAC to validate.
 *
 * @return
 *   TRUE if the data from SveaWebPay is valid.
 */
function _uc_sveawebpay_validatemac($data_to_validate, $mac_to_validate) {
  $mac_string = hash("SHA512", $data_to_validate . variable_get('uc_svea_secretword', ''));
  return $mac_to_validate == $mac_string;
}

/**
 * Handles return data from SveaWebPay.
 *
 * Checks that the return data is valid, that no error occured during payment
 * and updates the order according to returned data.
 *  $_SESSION contains the (from SveaWebPay) posted array with return data.
 */
function uc_sveawebpay_complete(&$form_state) {
  if ($_SESSION['invoice_confirmed'] ) {
    unset($_SESSION['invoice_confirmed']);
   // Load the order.
    $order_id = intval($_SESSION['cart_order']);
    $order = uc_order_load($order_id);

    // Register the order as payed.
    $comment = t('Paid with SveaWebPay, Invoice');
    uc_payment_enter($order->order_id, 'sveawebpay_hosted_payment', $order->order_total , 0, NULL, $comment);
    uc_order_update_status($order_id, 'Svea_Invoice_pending');
    drupal_goto('cart/checkout/complete');
  }
  $response = _uc_sveawebpay_decode_response($_REQUEST['response']);
  $return_mac = $_REQUEST['mac'];
  $good_mac = _uc_sveawebpay_validatemac($_REQUEST['response'], $return_mac);

  // Handle possibly errors in return from SveaWebPay.
  if ($response['response']['statuscode'] != UC_SVEAWEBPAY_SUCCESS || !$good_mac) {
    
    $error = _uc_sveawebpay_error_handler($response['response']['statuscode']);
    if ($response['response']['statuscode'] == 108) { // Cancel by user
      drupal_set_message(t('Your payment was cancelled. Please feel free to continue shopping or contact us for assistance.'));
    }
    elseif ($response['response']['statuscode'] == 0 && !$good_mac) {
      drupal_set_message(t('MAC error'), 'error');
      $error = array('name' => 'MAC Error', 'description' => t('Invalid MAC in return from SveaWebPay.'));
    }
    else {
      drupal_set_message(t('Error code: @error_code - @description', array('@error_code' => $response['response']['statuscode'], '@description' => $error ['description'])), 'error');
    }
    // Change orderstatus to canceled and log errorcodes in order.
    uc_order_update_status($_SESSION['cart_order'], uc_order_state_default('canceled'));
    uc_order_comment_save($_SESSION['cart_order'], 0, t('Transaction error from SveaWebPay. Error code: @error_code - $name', array('@error_code' => $response['response']['statuscode'], '@error_name' => $error ['description'])), 'admin');

    unset($_SESSION['cart_order']);

    drupal_goto('cart');
  }
  // Handle good payment!
  $order = uc_order_load(intval($response['response']['transaction']['customerrefno']));

  // Reroute payment made with the invoice to an secondary confirm.
  if ($response['response']['transaction']['paymentmethod'] == 'SVEAINVOICESE') {
    $output = _uc_sveawebpay_invoice_form($form_state, $response);
  }
  else {
    // If the order ID in the return URL is not the same as the one in the
    // user's session, we assume this is a spoof.
    if (intval($_SESSION['cart_order']) != $order->order_id) {
      drupal_set_message(t('Thank you for your order! SveaWebPay will notify us once your payment has been processed.'));
      drupal_goto('cart');
    }
    // Ensure the payment method is SveaWebPay.
    if ($order->payment_method != 'sveawebpay_hosted_payment') {
      drupal_goto('cart');
    }
    // This lets us know it's a legitimate access of the complete page.
    $_SESSION['do_complete'] = TRUE;

    $comment = t('Paid with SveaWebPay, @type', array(
      '@type' => $response['response']['transaction']['paymentmethod'],
    ));
    // Move decimal 2 pos left.
    $amount = $response['response']['transaction']['amount'] / 100; 
    uc_payment_enter($order->order_id, 'sveawebpay_hosted_payment', $amount , 0, NULL, $comment);

    drupal_goto('cart/checkout/complete');
  }

  return $output;
}


/**
 * Handles an invoice payment.
 *
 * Invoive payments requires an extra confirmation from the customer. It may
 * also end with an extra invoice fee that have to be added to the order.
 *
 * @param $response
 *   An array containing an santized version of the response from SveaWebPay.
 *
 */
function _uc_sveawebpay_invoice_form(&$form_state, $response) {
  $form = array();
  if (!isset($form_state['storage']['step'])) {
    $form_state['storage']['step'] = 1;
  }
  else {
    $form_state['storage']['step'] = 2;
  }

  if (empty( $_SERVER['HTTPS'])) {
    drupal_set_message(t('Your site is not running with SSL enabled! Private invoice data is therefor sent unecrypted!'), 'warning' );
  }
  if ($_SESSION['cart_order'] == $response['response']['transaction']['customerrefno']) {

    // This lets us know it's a legitimate access of the complete page.
    $_SESSION['do_complete'] = TRUE;
    $_SESSION['invoice_confirmed'] = TRUE;
    $order_id = intval($response['response']['transaction']['customerrefno']);
    $order = uc_order_load($order_id);

    // Update $order->deliveryXXX with return data from Svea.
    $delivery_name = explode(',', $response['response']['transaction']['customer']['legalname']);
    $order->delivery_first_name =  $delivery_name[1];
    $order->delivery_last_name =  $delivery_name[0];
    $order->delivery_street1 = $response['response']['transaction']['customer']['addressline1'];
    $order->delivery_street2 = $response['response']['transaction']['customer']['addressline2'];
    $order->delivery_postal_code = $response['response']['transaction']['customer']['postcode'];
    $order->delivery_city = $response['response']['transaction']['customer']['postarea'];
    // In this version SveaWebPay Faktura only delivers to Sweden.
    $order->delivery_country = '752'; 

    $context = array(
      'revision' => 'themed',
      'type' => 'order',
      'class' => array('product', 'sell_price'),
      'subject' => array('node' => $node),
    );
    // Update order with invoice fee.
    $payment_amount = preg_replace('/[^0-9]*/', '', uc_price($response['response']['transaction']['amount']/100, $context));
    $order_amount = preg_replace('/[^0-9]*/', '', uc_price($order->order_total, $context));

    uc_order_save($order);
    if (variable_get('uc_svea_invoice_fee', '')) {
      $invoice_fee = t('Invoice fee');
      if (variable_get('uc_svea_invoice_feeVAT', '')) {
        $invoice_fee_value = str_replace(',', '.', variable_get('uc_svea_invoice_fee', ''));
        $invoice_fee .= ' (' . t('where of') . ' ' . variable_get('uc_svea_invoice_feeVAT', '') . ' ' . t('VAT') . ')';
      }
      uc_order_line_item_add($order_id, 'generic', $invoice_fee, $invoice_fee_value, 1);
    }

    $path = base_path() . drupal_get_path('module', 'uc_sveawebpay');
    drupal_add_css(drupal_get_path('module', 'uc_sveawebpay') . '/uc_sveawebpay.css');
    $output = '<div class="confirm-invoice"><img src="' . $path . '/images/Faktura-100px.png" style="position: relative; top: 5px;" /><h3>' . t('Confirm payment on') . ' ' . uc_price($payment_amount/100) . '</h3>';
    if (variable_get('uc_svea_invoice_fee', '')) {
      $output .= '<span class=invoice_vat> ' . t('(inclusive invoice fee on') . uc_price($invoice_fee_value) . ')</span>';
    }
    $header = array(t('Name'), t('Delivery address'));
    $row = array();
    $row[] = array($order->delivery_first_name . ' ' . $order->delivery_last_name, $order->delivery_street1);
    $row[] = array('', $order->delivery_street2);
    $row[] = array('', $order->delivery_postal_code . ' ' . $order->delivery_city);
    $output .= theme_table($header, $row);

    $output .= '<p>' . t("Your order is not complete until you click the 'Submit order' button below. You will be invoiced for the amount shown above once your order is sent.") . '</p>';
    $output .= '</div>';

    // Fake the checkout form delivery information.
    $form['order'] = array('#type' => 'hidden', '#value' => $order->order_id);
    $form['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Submit order'),
      '#prefix' => $output,
    );
    $form['#validate'][] = '_uc_sveawebpay_invoice_form_validate';

  }
  return $form;
}

// Submit confirmation of the invoice back to SveaWebPay.
function _uc_sveawebpay_invoice_form_validate($form, &$form_state) {
  if (isset($_SESSION['cart_order'])) {
    $order = uc_order_load($_SESSION['cart_order']);
    $comment = t('Paid with SveaWebPay, @type', array(
        '@type' => $response['response']['transaction']['paymentmethod'],
    ));
    // Move decimal 2 pos left.
    $amount = $response['response']['transaction']['amount'] / 100;
    uc_payment_enter($order->order_id, 'sveawebpay_hosted_payment', $amount , 0, NULL, $comment);
    uc_order_update_status($order_id, 'Svea_Invoice_pending');
  }
  drupal_goto('cart/checkout/complete');
}

/**
 * Errorhandler, if $status_code != UC_SVEAWEBPAY_SUCCESS.
 */
function _uc_sveawebpay_error_handler($status_code) {
  switch ($status_code) {
    case 100:
      $error = array(
        'name' => 'INTERNAL_ERROR',
        'description' => t('Invalid - contact integrator.'),
      );
      break;
    case 101:
      $error = array(
        'name' => 'XMLPARSEFAIL',
        'description' => t('Invalid XML.'),
      );
      break;
    case 102:
      $error = array(
        'name' => 'ILLEGAL_ENCODING',
        'description' => t('Invalid encoding.'),
      );
      break;
    case 104:
      $error = array(
        'name' => 'ILLEGAL_URL',
        'description' => '',
      );
      break;
    case 105:
      $error = array(
        'name' => 'ILLEGAL_TRANSACTIONSTATUS',
        'description' => t('Invalid transaction status.'),
      );
      break;
    case 106:
      $error = array(
        'name' => 'EXTERNAL_ERROR',
        'description' => t('Failure at third party e.g. failure at the bank.'),
      );
      break;
    case 107:
      $error = array(
        'name' => 'DENIED_BY_BANK',
        'description' => t('Transaction rejected by bank.'),
      );
      break;
    case 108:
      $error = array(
        'name' => 'CANCELLED',
        'description' => t('Transaction cancelled.'),
      );
      break;
    case 109:
      $error = array(
        'name' => 'NOT_FOUND_AT_BANK',
        'description' => t('Transaction not found at the bank.'),
      );
      break;
    case 110:
      $error = array(
        'name' => 'ILLEGAL_TRANSACTIONID',
        'description' => t('Invalid transaction ID.'),
      );
      break;
    case 111:
      $error = array(
        'name' => 'MERCHANT_NOT_CONFIGURED',
        'description' => t('Merchant not configured.'),
      );
      break;
    case 112:
      $error = array(
        'name' => 'MERCHANT_NOT_CONFIGURED_AT_BANK',
        'description' => t('Merchant not configured at the bank.'),
      );
      break;
    case 113:
      $error = array(
        'name' => 'PAYMENTMETHOD_NOT_CONFIGURED',
        'description' => t('Payment method not configured for merchant.'),
      );
      break;
    case 114:
      $error = array(
        'name' => 'TIMEOUT_AT_BANK',
        'description' => t('Timeout at the bank.'),
      );
      break;
    case 115:
      $error = array(
        'name' => 'MERCHANT_NOT_ACTIVE',
        'description' => t('The merchant is disabled.'),
      );
      break;
    case 116:
      $error = array(
        'name' => 'PAYMENTMETHOD_NOT_ACTIVE',
        'description' => t('The payment method is disabled.'),
      );
      break;
    case 117:
      $error = array(
        'name' => 'ILLEGAL_AUTHORIZED_AMOUNT',
        'description' => t('Invalid authorized amount.'),
      );
      break;
    case 118:
      $error = array(
        'name' => 'ILLEGAL_CAPTURED_AMOUNT',
        'description' => t('Invalid captured amount.'),
      );
      break;
    case 119:
      $error = array(
        'name' => 'ILLEGAL_CREDITED_AMOUNT',
        'description' => t('Invalid credited amount.'),
      );
      break;
    case 120:
      $error = array(
        'name' => 'NOT_SUFFICIENT_FUNDS',
        'description' => t('Not enough founds.'),
      );
      break;
    case 121:
      $error = array(
        'name' => 'EXPIRED_CARD',
        'description' => t('The card has expired.'),
      );
      break;
    case 122:
      $error = array(
        'name' => 'STOLEN_CARD',
        'description' => t('Stolen card.'),
      );
      break;
    case 123:
      $error = array(
        'name' => 'LOST_CARD',
        'description' => t('Lost card.'),
      );
      break;
    case 124:
      $error = array(
        'name' => 'EXCEEDS_AMOUNT_LIMIT',
        'description' => t('Amount exceeds the limit.'),
      );
      break;
    case 125:
      $error = array(
        'name' => 'EXCEEDS_FREQUENCY_LIMIT',
        'description' => t('Frequency limit exceeded.'),
      );
      break;
    case 126:
      $error = array(
        'name' => 'TRANSACTION_NOT_BELONGING_TO_MERCHANT',
        'description' => t('Transaction does not belong to merchant.'),
      );
      break;
    case 127:
      $error = array(
        'name' => 'CUSTOMERREFNO_ALREADY_USED',
        'description' => t('Customer reference number already used in another transaction.'),
      );
      break;
    case 128:
      $error = array(
        'name' => 'NO_SUCH_TRANS',
        'description' => t('Transaction does not exist.'),
      );
      break;
    case 129:
      $error = array(
        'name' => 'DUPLICATE_TRANSACTION',
        'description' => t('More than one transaction found for the given customer reference number.'),
      );
      break;
    case 130:
      $error = array(
        'name' => 'ILLEGAL_OPERATION',
        'description' => t('Operation not allowed for the given.'),
      );
      break;
    case 131:
      $error = array(
        'name' => 'COMPANY_NOT_ACTIVE',
        'description' => t('Company inactive.'),
      );
      break;
    case 132:
      $error = array(
        'name' => 'SUBSCRIPTION_NOT_FOUND',
        'description' => t('No subscription exist.'),
      );
      break;
    case 133:
      $error = array(
        'name' => 'SUBSCRIPTION_NOT_ACTIVE',
        'description' => t('Subscription not active.'),
      );
      break;
    case 134:
      $error = array(
        'name' => 'SUBSCRIPTION_NOT_SUPPORTED',
        'description' => t('Payment method doesn’t support subscriptions.'),
      );
      break;
    case 135:
      $error = array(
        'name' => 'ILLEGAL_DATE_FORMAT',
        'description' => t('Illegal date format.'),
      );
      break;
    case 136:
      $error = array(
        'name' => 'ILLEGAL_RESPONSE_DATA',
        'description' => t('Illegal response data.'),
      );
      break;
    case 137:
      $error = array(
        'name' => 'IGNORE_CALLBACK',
        'description' => t('Ignore callback.'),
      );
      break;
    case 138:
      $error = array(
        'name' => 'CURRENCY_NOT_CONFIGURED',
        'description' => t('Currency not configured.'),
      );
      break;
    case 139:
      $error = array(
        'name' => 'CURRENCY_NOT_ACTIVE',
        'description' => t('Currency not active.'),
      );
      break;
    case 140:
      $error = array(
        'name' => 'CURRENCY_ALREADY_CONFIGURED',
        'description' => t('Currency is already configured.'),
      );
      break;
    case 141:
      $error = array(
        'name' => 'ILLEGAL_AMOUNT_OF_RECURS_TODAY',
        'description' => t('Ilegal amount of recurs per day.'),
      );
      break;
    case 142:
      $error = array(
        'name' => 'NO_VALID_PAYMENT_METHODS',
        'description' => t('No valid paymentmethods.'),
      );
      break;
    case 300:
      $error = array(
        'name' => 'BAD_CARDHOLDER_NAME',
        'description' => t('Invalid value for cardholder name.'),
      );
      break;
    case 301:
      $error = array(
        'name' => 'BAD_TRANSACTION_ID',
        'description' => t('Invalid value for transaction id.'),
      );
      break;
    case 302:
      $error = array(
        'name' => 'BAD_REV',
        'description' => t('Invalid value for rev.'),
      );
      break;
    case 303:
      $error = array(
        'name' => 'BAD_MERCHANT_ID ',
        'description' => t('Invalid value for merchant id.'),
      );
      break;
    case 304:
      $error = array(
        'name' => 'BAD_LANG ',
        'description' => t('Invalid value for lang.'),
      );
      break;
    case 305:
      $error = array(
        'name' => 'BAD_AMOUNT ',
        'description' => t('Invalid value for amount.'),
      );
      break;
    case 306:
      $error = array(
        'name' => 'BAD_CUSTOMERREFNO',
        'description' => t('Invalid value for customer refno.'),
      );
      break;
    case 307:
      $error = array(
        'name' => 'BAD_CURRENCY',
        'description' => t('Invalid value for currency.'),
      );
      break;
    case 308:
      $error = array(
        'name' => 'BAD_PAYMENTMETHOD',
        'description' => t('Invalid value for payment method.'),
      );
      break;
    case 309:
      $error = array(
        'name' => 'BAD_RETURNURL',
        'description' => t('Invalid value for return url.'),
      );
      break;
    case 310:
      $error = array(
        'name' => 'BAD_LASTBOOKINGDAY',
        'description' => t('Invalid value for last booking day.'),
      );
      break;
    case 311:
      $error = array(
        'name' => 'BAD_MAC',
        'description' => t('Invalid value for mac.'),
      );
      break;
    case 312:
      $error = array(
        'name' => 'BAD_TRNUMBER',
        'description' => t('Invalid value for tr number.'),
      );
      break;
    case 313:
      $error = array(
        'name' => 'BAD_AUTHCODE',
        'description' => t('Invalid value for authcode.'),
      );
      break;
    case 314:
      $error = array(
        'name' => 'BAD_CC_DESCR',
        'description' => t('Invalid value for cc_descr.'),
      );
      break;
    case 315:
      $error = array(
        'name' => 'BAD_ERROR_CODE',
        'description' => t('Invalid value for error_code.'),
      );
      break;
    case 316:
      $error = array(
        'name' => 'BAD_CARDNUMBER_OR_CARDTYPE_NOT_CONFIGURED',
        'description' => t('Card type not configured for merchant.'),
      );
      break;
    case 317:
      $error = array(
        'name' => 'BAD_SSN',
        'description' => t('Invalid value for ssn.'),
      );
      break;
    case 318:
      $error = array(
        'name' => 'BAD_VAT',
        'description' => t('Invalid value for vat.'),
      );
      break;
    case 319:
      $error = array(
        'name' => 'BAD_CAPTURE_DATE',
        'description' => t('Invalid value for capture date.'),
      );
      break;
    case 320:
      $error = array(
        'name' => 'BAD_CAMPAIGN_CODE_INVALID',
        'description' => t('Invalid value for campaign code. There are no valid matching campaign codes.'),
      );
      break;
    case 321:
      $error = array(
        'name' => 'BAD_SUBSCRIPTION_TYPE',
        'description' => t('Invalid subscription type.'),
      );
      break;
    case 322:
      $error = array(
        'name' => 'BAD_SUBSCRIPTION_ID',
        'description' => t('Invalid subscription id.'),
      );
      break;
    case 323:
      $error = array(
        'name' => 'BAD_BASE64',
        'description' => t('Invalid base64.'),
      );
      break;
    case 324:
      $error = array(
        'name' => 'BAD_CAMPAIGN_CODE',
        'description' => t('Invalid campaign code. Missing value.'),
      );
      break;
    case 325:
      $error = array(
        'name' => 'BAD_CALLBACKURL',
        'description' => t('Invalid callbackurl.'),
      );
      break;
    case 500:
      $error = array(
        'name' => 'ANTIFRAUD_CARDBIN_NOT_ALLOWED',
        'description' => t('Antifraud - cardbin not allowed.'),
      );
      break;
    case 501:
      $error = array(
        'name' => 'ANTIFRAUD_IPLOCATION_NOT_ALLOWED',
        'description' => t('Antifraud – iplocation not allowed.'),
      );
      break;
    default:
      $error = array(
        'name' => 'UNKNOWN',
        'description' => t('Unknown error!'),
    );
  }
  return $error;
}

/**
 * Handles decoding and transforming of XML-response from SveaWebPay
 * to an array.
 *
 * @param array $xml_data
 *   An array containing XML-data.
 * @param bool $encoded
 *   TRUE if XML-data is base64 encoded.
 *
 * @return
 *   Array containing the corresponding data from XML.
 */
function _uc_sveawebpay_decode_response($xml_data, $encoded = TRUE) {
  $xml = $encoded ? base64_decode($xml_data) : $xml_data;

  $parser = xml_parser_create('UTF-8');
  xml_parser_set_option($parser, XML_OPTION_CASE_FOLDING, 0);
  xml_parse_into_struct($parser, $xml, $values);
  xml_parser_free($parser);
  // The returned array.
  $return = array();
  // Temp array used for stacking.
  $stack = array();
  foreach ($values as $val) {
    if ($val['type'] == "open") {
      array_push($stack, $val['tag']);
    }
    elseif ($val['type'] == "close") {
      array_pop($stack);
    }
    elseif ($val['type'] == "complete") {
      array_push($stack, $val['tag']);
      _uc_sveawebpay_setArrayValue($return, $stack, $val['value']);
      array_pop($stack);
    }
  }
  return $return;
}
/**
 * Helper for decoding XML.
 */
function _uc_sveawebpay_setArrayValue(&$array, $stack, $value) {
  if ($stack) {
    $key = array_shift($stack);
    _uc_sveawebpay_setArrayValue($array[$key], $stack, $value);
    return $array;
  }
  else {
    $array = check_plain($value);
  }
}
