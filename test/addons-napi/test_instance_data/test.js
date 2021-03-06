'use strict';
// Test API calls for instance data.

const common = require('../../common');
const assert = require('assert');

if (module.parent) {
  // When required as a module, run the tests.
  const test_instance_data =
    require(`./build/${common.buildType}/test_instance_data`);

  // Print to stdout when the environment deletes the instance data. This output
  // is checked by the parent process.
  test_instance_data.setPrintOnDelete();

  // Test that instance data can be accessed from a binding.
  assert.strictEqual(test_instance_data.increment(), 42);

  // Test that the instance data can be accessed from a finalizer.
  test_instance_data.objectWithFinalizer(common.mustCall());
  global.gc();

  // Test that instance data can be used in an async work callback.
  new Promise((resolve) => test_instance_data.asyncWorkCallback(resolve))

    // Test that the buffer finalizer can access the instance data.
    .then(() => new Promise((resolve) => {
      test_instance_data.testBufferFinalizer(resolve);
      global.gc();
    }))

    // Test that the thread-safe function can access the instance data.
    .then(() => new Promise((resolve) =>
      test_instance_data.testThreadsafeFunction(common.mustCall(),
                                                common.mustCall(resolve))));
} else {
  // When launched as a script, run tests in either a child process or in a
  // worker thread.
  const requireAs = require('../../common/require-as');
  const runOptions = { stdio: ['inherit', 'pipe', 'inherit'] };

  function checkOutput(child) {
    assert.strictEqual(child.status, 0);
    assert.strictEqual(
      (child.stdout.toString().split(/\r\n?|\n/) || [])[0],
      'deleting addon data');
  }

  // Run tests in a child process.
  checkOutput(requireAs(__filename, ['--expose-gc', '--experimental-worker'],
                        runOptions, 'child'));

  // Run tests in a worker thread in a child process.
  checkOutput(requireAs(__filename, ['--expose-gc', '--experimental-worker'],
                        runOptions, 'worker'));
}
